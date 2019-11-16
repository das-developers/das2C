/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu> 
 *                         Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <expat.h>

/* #include <das.h> */

#include "packet.h"


/* ************************************************************************* */
/* Construction/Destruction */

PktDesc* new_PktDesc() {
    PktDesc* pd;
    int iplane;
    pd= (PktDesc*)calloc(1, sizeof(PktDesc));
    pd->base.type= PACKET;
	 
	 /* Redundant */
    for ( iplane=0; iplane<MAXPLANES; iplane++ ) {
        pd->planes[iplane]= NULL;
    }

    return pd;
}

struct parse_pktdesc_stack{
 	 PktDesc* pd;
	 
	 int errorCode;      /* error code */
    char errorMsg[DAS_XML_NODE_NAME_LEN];

    DasDesc* currentDesc;
};		

/* Formerly nested function "start" in parsePktDesc */
void PktDesc_parseStart(void *data, const char *el, const char **attr) {

	int i = 0;
	struct parse_pktdesc_stack* pStack = (struct parse_pktdesc_stack*)data;
	char sType[64] = {'\0'};
	char sName[64] = {'\0'};
	const char* pColon = NULL;
	 
	/* Get the non-plane stuff out of the way first, it's easy */
	if ( strcmp( el, "properties" )==0 ) {
		for (i=0; attr[i]; i+=2) {
			if( (pColon = strchr(attr[i], ':')) != NULL){
				memset(sType, '\0', 64);
				strncpy(sType, attr[i], pColon - attr[i]);
				strncpy(sName, pColon+1, 63);
				DasDesc_set(pStack->currentDesc, sType, sName, attr[i+1] );
			}
			else{
				DasDesc_set(pStack->currentDesc, "String", attr[i], attr[i+1]);
			}
		}
		return;
	}

	if ( strcmp( el, "packet" )==0 ) {
		pStack->currentDesc = (DasDesc*)(pStack->pd);
		
		for (i=0; attr[i]; i+=2){
			if(strcmp(attr[i], "group") == 0){
				PktDesc_setGroup(pStack->pd, (char*)attr[i+1]);
			}
		}
		return;
	}

	/* This had better be a plane */
	if( strcmp( el, "x" ) !=0    && strcmp( el, "y" )!=0 && 
	    strcmp( el, "yscan" )!=0 && strcmp(el, "z")!=0){
		pStack->errorCode= DASERR_PKT;
		snprintf(pStack->errorMsg, DAS_XML_NODE_NAME_LEN - 1, 
		         "Unsupported tag in <packet> element: %s\n",el);
		return;		
	}
	
	plane_type_t pt = str2PlaneType(el);
	
	/* Use the keyword/value constructor for the plane object */
	PlaneDesc* pPlane = new_PlaneDesc_pairs((DasDesc*)pStack->pd, pt, attr);
	if(pPlane == NULL){
		pStack->errorCode= DASERR_PKT;
		snprintf(
			pStack->errorMsg,  DAS_XML_NODE_NAME_LEN - 1, 
			"Failed to plane at index %zu from packet %02d XML header",
			pStack->pd->uPlanes, pStack->pd->id
		);
		return;
	}
	pStack->pd->planes[pStack->pd->uPlanes] = pPlane;
	pStack->pd->uPlanes += 1;
	
	pStack->currentDesc = (DasDesc*)pPlane;
}

void PktDesc_parseEnd(void *data, const char *el)
{	
	struct parse_pktdesc_stack* pStack = (struct parse_pktdesc_stack*)data;
	
	/* Plane is over, set current desc back to pkt desc */
	if( strcmp( el, "x" )==0  ||  strcmp( el, "y" )==0  ||  
	    strcmp( el, "yscan" )==0 || strcmp( el, "z" )==0) {
		  pStack->currentDesc = (DasDesc*)pStack->pd;
	 }
}

/* PktDesc* new_PktDesc_xml(Descriptor* pParent, int nPktId, const char* sXML)*/
PktDesc* new_PktDesc_xml(DasBuf* pBuf, DasDesc* pParent, int nPktId)
{
	PktDesc* pThis = new_PktDesc();
	size_t uPos = DasBuf_readOffset(pBuf);
	
	struct parse_pktdesc_stack stack = {0};
	 
	stack.pd        = pThis;
	stack.errorCode = 0;    /* error code */
  
	XML_Parser p    = XML_ParserCreate("UTF-8"); 
	if ( !p ) {
		das_error(DASERR_PKT, "Couldn't create XML parser\n" );
		return NULL;
	}
	XML_SetUserData(p, (void*)&stack);
	XML_SetElementHandler(p, PktDesc_parseStart, PktDesc_parseEnd);

	pThis->base.properties[0]= NULL;
	
	int nParRet = XML_Parse( p, pBuf->pReadBeg, DasBuf_unread(pBuf), true );
	if ( !nParRet) {
        das_error(DASERR_PKT, "Parse error at offset %ld:\n%s\n",
            XML_GetCurrentByteIndex(p),
            XML_ErrorString(XML_GetErrorCode(p)) );
	}
	XML_ParserFree(p);

	if ( stack.errorCode != 0 ) { 
        das_error( stack.errorCode, stack.errorMsg );
		  del_PktDesc(pThis);
		  return NULL;
	}
    
	if(pThis->uPlanes == 0){
		DasBuf_setReadOffset(pBuf, uPos);
		das_error(DASERR_PKT, "No data planes found! Here is the XML packet header:\n%s",
		           pBuf->pReadBeg);
		del_PktDesc(pThis);
		return NULL;
	}
	 
	pThis->base.parent = pParent;
	pThis->id = nPktId;
	return pThis;
}


void del_PktDesc(PktDesc* pThis) {
	size_t u = 0;
	for(u = 0; u < pThis->uPlanes; u++ ){
		del_PlaneDesc(pThis->planes[u]);
	}
	
	DasDesc_freeProps(&(pThis->base));
	free(pThis);
}

/* ************************************************************************* */
/* Checking equality */
bool PktDesc_equalFormat(const PktDesc* pPd1, const PktDesc* pPd2)
{
	PlaneDesc* pPlane1 = NULL;
	PlaneDesc* pPlane2 = NULL;
	
	if(pPd1->uPlanes != pPd2->uPlanes) return false;
	
	if(! DasDesc_equals((DasDesc*)pPd1, (DasDesc*)pPd2)) return false;
	
	for(size_t u = 0; u<pPd1->uPlanes; u++){
		pPlane1 = pPd1->planes[u];
		pPlane2 = pPd2->planes[u];
		if(pPlane1->planeType != pPlane2->planeType) return false;
		if(pPlane1->uItems != pPlane2->uItems) return false;
		if(! DasEnc_equals(pPlane1->pEncoding, pPlane2->pEncoding)) return false;
		if( strcmp(pPlane1->units, pPlane2->units) != 0) return false;
		
		if(pPlane1->planeType == YScan){
			if(! DasEnc_equals(pPlane1->pYEncoding, pPlane2->pYEncoding)) return false;
			if( strcmp(pPlane1->yTagUnits, pPlane2->yTagUnits) != 0) return false;
			
			if((pPlane1->pYTags != NULL)&&(pPlane1->pYTags == NULL)) return false;
			if((pPlane1->pYTags == NULL)&&(pPlane1->pYTags != NULL)) return false;
			
			if((pPlane1->pYTags != NULL)&&(pPlane1->pYTags != NULL)){				
				for(size_t v = 0; v<pPlane1->uItems; v++)
					if(pPlane1->pYTags[v] != pPlane2->pYTags[v] ) return false;
			}
			
		}
		
		if(pPlane1->sName && !(pPlane2->sName)) return false;
		if(!(pPlane1->sName) && pPlane2->sName) return false;
		if(pPlane1->sName && pPlane2->sName && 
		   strcmp(pPlane1->sName, pPlane2->sName) != 0) return false;
		
		if(! DasDesc_equals((DasDesc*)pPlane1, (DasDesc*)pPlane2) ) return false;
	}
	
	return true;
}

/* ************************************************************************* */
/* Adding Sub-objects */

int PktDesc_addPlane(PktDesc* pThis, PlaneDesc* pPlane)
{
	if(pThis->uPlanes >= MAXPLANES){
		das_error(DASERR_PKT, "Too many planes, limit is %d\n", MAXPLANES );
		return -1;
	}
	
	/* Check for legal patterns, don't want to mess up the client programs to bad */
	/* if(pPlane->planeType == X){
		if(PktDesc_getNPlanesOfType(pThis, X) > 1){
			das_error(DASERR_PKT, "There can only be a maximum of 2 X-planes in a packet");
			return -1;
		}
	}*/
	if(pPlane->planeType == YScan){
		if(PktDesc_getNPlanesOfType(pThis, Z) > 0){
			das_error(DASERR_PKT, "YScan and Z planes cannot be present in the same packet");
			return -1;
		}
	}
	if(pPlane->planeType == Z){
		if(PktDesc_getNPlanesOfType(pThis, YScan) > 0){
			das_error(DASERR_PKT, "Z and YScan planes cannot be present in the same packet");
			return -1;
		}
	}
	
	int iPos = pThis->uPlanes;
	pThis->uPlanes += 1;
	pThis->planes[iPos] = pPlane;
	
	pPlane->base.parent = (DasDesc*)pThis;
	
	return iPos;
}

DasErrCode PktDesc_copyPlanes(PktDesc* pThis, const PktDesc* pOther)
{
	/* I can imagine merge scenarios, maybe this check should be removed... */
	if(pThis->uPlanes > 0)
		return das_error(DASERR_PKT, "ERROR: Can't use copyPlanes here, packet type "
		              "%02d already has 1 or more planes defined\n",  pThis->id);
	
	/* put the new planes in the same order as the existing ones, but skip any
	 * gaps */
	PlaneDesc* pPlane = NULL;
	for(int i = 0; i < MAXPLANES && i < pOther->uPlanes; i++){
		if(pOther->planes[i] == NULL){
			fprintf(stderr, "WARNING: Gap in plane definitions detected\n");
			continue;
		}
		
		pPlane = PlaneDesc_copy(pOther->planes[i]);
		if(PktDesc_addPlane(pThis, pPlane) == -1) return 18;
	}
	
	return 0;
}

bool PktDesc_validate(PktDesc* pThis ) {
   /* Make sure the dependent planes are present */
	if((PktDesc_getNPlanesOfType(pThis, Y) > 0) && 
		(PktDesc_getNPlanesOfType(pThis, X) == 0)){
		das_error(DASERR_PKT, "In packet type %02d, Y planes are present without an X "
				     "plane", pThis->id);
		return false;
	}
	
	if((PktDesc_getNPlanesOfType(pThis, YScan) > 0) && 
		(PktDesc_getNPlanesOfType(pThis, X) == 0)){
		das_error(DASERR_PKT, "In packet type %02d, YScan planes are present without an X "
				     "plane", pThis->id);
		return false;
	}
	
	if((PktDesc_getNPlanesOfType(pThis, Z) > 0) && 
		(PktDesc_getNPlanesOfType(pThis, Y) == 0)){
		das_error(DASERR_PKT, "In packet type %02d, Z planes are present without a Y "
				     "plane", pThis->id);
		return false;
	}
	return true;
}

/* ************************************************************************** */
/* Convenience routines to cut client code bloat */

DasErrCode PktDesc_setValue(
	PktDesc* pThis, size_t uPlane, size_t uItem, double val
){
	PlaneDesc* pPlane = PktDesc_getPlane(pThis, uPlane);
	if(pPlane == NULL){ 
		return das_error(DASERR_PKT, "Plane index %02d is not defined for packet type "
		                  "%02d ", pThis->id, pPlane);
	}
	
	return PlaneDesc_setValue(pPlane, uItem, val);
}

DasErrCode PktDesc_setValues(PktDesc* pThis, size_t uPlane, const double* pVals)
{
	PlaneDesc* pPlane = PktDesc_getPlane(pThis, uPlane);
	if(pPlane == NULL){ 
		return das_error(DASERR_PKT, "Plane index %02d is not defined for packet type "
		                  "%02d ", pThis->id, pPlane);
	}
	PlaneDesc_setValues(pPlane, pVals);
	return 0;
}

/* ************************************************************************** */
/* Getting info and sub objects */

int PktDesc_getId(const PktDesc* pThis){
	if(pThis->id == 0) return -1;
	if(pThis->id < 0 || pThis->id > 99)
		return das_error(17, "Packet Descriptor has Invalid packet ID: %d ", 
		                  pThis->id);
	return pThis->id;
}

const char* PktDesc_getGroup(const PktDesc* pThis ) {
	return pThis->sGroup;
}

void PktDesc_setGroup(PktDesc* pThis, const char* sGroup)
{
	size_t uOldLen = strlen(pThis->sGroup);
	size_t uNewLen = strlen(sGroup);
	if(sGroup == NULL){
		free(pThis->sGroup);
		pThis->sGroup = NULL;
	}
	else{
		if(uNewLen > uOldLen){
			pThis->sGroup = realloc(pThis->sGroup, uNewLen);
		}
		strncpy(pThis->sGroup, sGroup, uNewLen + 1);
	}
	pThis->bSentHdr = false;
}


size_t PktDesc_getNPlanesOfType(const PktDesc* pThis, plane_type_t pt)
{
	size_t uPlanes = 0;
	for(int i = 0; i < pThis->uPlanes; i++)
		if(pThis->planes[i]->planeType == pt) uPlanes++;
	
	return uPlanes;
}

size_t PktDesc_getNPlanes(const PktDesc* pThis){
	return pThis->uPlanes;
}

plane_type_t PktDesc_getPlaneType(const PktDesc* pThis, int iPlane)
{
	if(iPlane < 0 || iPlane >= MAXPLANES)
		das_error(DASERR_PKT, "ERROR: Invalid value for the plane index %d", iPlane);

	if(pThis->planes[iPlane] == NULL) 
		return Invalid;
	else
		return pThis->planes[iPlane]->planeType;
}

int PktDesc_getPlaneIdxByName(PktDesc* pThis, const char * name, plane_type_t planeType )
{
    int iplane;
    for ( iplane=0; iplane<pThis->uPlanes; iplane++ ) {
        if ( planeType==pThis->planes[iplane]->planeType && 
				 strcmp( name, pThis->planes[iplane]->sName )==0 ) {
            return iplane;
        }
    }
    return -1;
}

PlaneDesc* PktDesc_getPlaneByName(PktDesc* pThis, const char* name)
{
	int iplane;	
	
	for(iplane=0; iplane<pThis->uPlanes; iplane++ ) {
		if(strcmp( name, pThis->planes[iplane]->sName )==0 ) {
			return PktDesc_getPlane(pThis, iplane );
		}
    }
    return NULL;
}

PlaneDesc* PktDesc_getPlaneByType(
	PktDesc* pThis, plane_type_t ptype, int iRelIndex
){
	int iPlane = 0;
	int iCount = -1;
	
	for(iPlane = 0; iPlane < pThis->uPlanes; iPlane++){
		if(pThis->planes[iPlane]->planeType == ptype){
			iCount += 1;
			if(iCount == iRelIndex) return pThis->planes[iPlane];
		}
	}
	return NULL;
}



int PktDesc_getPlaneIdxByType(
	const PktDesc* pThis, plane_type_t ptype, int iRelIndex
){
	int iPlane = 0;
	int iCount = -1;
	
	for(iPlane = 0; iPlane < pThis->uPlanes; iPlane++){
		if(pThis->planes[iPlane]->planeType == ptype){
			iCount += 1;
			if(iCount == iRelIndex) return iPlane;
		}
	}
	return -1;
}

PlaneDesc* PktDesc_getXPlane(PktDesc* pThis ) {
	int iPlane = PktDesc_getPlaneIdxByType(pThis, X, 0);
	if(iPlane != -1) return pThis->planes[iPlane];
	return NULL;
}

PlaneDesc* PktDesc_getPlane(PktDesc* pThis, int iplane ) {
    if ( iplane<0 || iplane>pThis->uPlanes ) {
        das_error(DASERR_PKT, "invalid plane number, iplane=%d\n", iplane );
    }
    return pThis->planes[iplane];
}

int PktDesc_getPlaneIdx(PktDesc* pThis, PlaneDesc* pPlane) {
	for(int u = 0; u < pThis->uPlanes; ++u){
		if(pThis->planes[u] == pPlane) return (int)u;
	}
	return -1;
}

/* ************************************************************************* */
/* Encode Descriptor to XML */

DasErrCode PktDesc_encode(const PktDesc* pThis, DasBuf* pBuf)
{
	DasErrCode nRet = 18;
	
	if(pThis->sGroup != NULL)
		nRet = DasBuf_printf(pBuf, "<packet group=\"%s\">\n", pThis->sGroup);
	else
		nRet = DasBuf_printf(pBuf, "<packet>\n");
	if(nRet != 0) return nRet;
		
	if( (nRet = DasDesc_encode((DasDesc*)pThis, pBuf, "  ")) != 0) return nRet;
	
	int iplane;	
	
	for(iplane=0; iplane < pThis->uPlanes; iplane++ ) {
		switch(pThis->planes[iplane]->planeType){
		case X:
			nRet = PlaneDesc_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		case Y:
			nRet = PlaneDesc_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		case Z:
		case YScan:
			/* Z and YScan planes are always dependent data */
			nRet = PlaneDesc_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		default:
			return das_error(DASERR_PKT, "Code change detected in PktDesc_encode");
			break;
		}
		if(nRet != 0) return nRet;
    }
	return DasBuf_printf(pBuf, "</packet>\n");
}

/* ************************************************************************* */
/* Data I/O */

size_t PktDesc_recBytes(const PktDesc* pThis)
{
	size_t uRecBytes = 0;
	PlaneDesc* pPlane = NULL;
	for(size_t u = 0; u < pThis->uPlanes; u++){
		pPlane = pThis->planes[u];
		uRecBytes += pPlane->pEncoding->nWidth * pPlane->uItems;
	}
	return uRecBytes;
}

DasErrCode PktDesc_decodeData(PktDesc* pThis, DasBuf* pBuf){
	
	size_t uRecBytes = PktDesc_recBytes(pThis);
	
	if(DasBuf_unread(pBuf) < uRecBytes){
		return das_error(DASERR_PKT, "For packet type %02d, %d bytes expected in each "
				            "packet only received %d", pThis->id, uRecBytes, 
				            DasBuf_unread(pBuf));
	}
	
	int nRet = 0;
	PlaneDesc* pPlane = NULL;
	for(int i = 0; i<pThis->uPlanes; i++){
		pPlane = pThis->planes[i];
		if( (nRet = PlaneDesc_decodeData(pPlane, pBuf)) != 0) break;
	}
	
	return nRet;
}


DasErrCode PktDesc_encodeData(const PktDesc* pThis, DasBuf* pBuf){
	DasErrCode nRet = 0;
	PlaneDesc* pPlane = NULL;
	bool bLast = false;
	
	/* check that everything get's written */
	size_t uBeg = DasBuf_written(pBuf);
	for(size_t u = 0; u < pThis->uPlanes; u++){
		pPlane = pThis->planes[u];
		if(u == pThis->uPlanes - 1) bLast = true;
		if( (nRet = PlaneDesc_encodeData(pPlane, pBuf, bLast)) != 0) return nRet;
	}
	
	/* TODO: Only run this check on the first pkt, saves un-needed calucations */
	size_t uEnd = DasBuf_written(pBuf);
	size_t uRecBytes = PktDesc_recBytes(pThis);
	if((uEnd - uBeg) != uRecBytes) 
		return das_error(DASERR_PKT, "Partial packet written expected output %zu bytes, "
				            "wrote %zu bytes instead.", uEnd - uBeg, uRecBytes);
	return 0;
}

/* Diagnostic, dump to standard error */
void PktDesc_dump(const PktDesc* pThis, FILE* file)
{
	DasBuf* pBuf = new_DasBuf(16000);
	char sEncoding[128] = {'\0'};
	
	size_t uRecBytes = PktDesc_recBytes(pThis);
	DasBuf_printf(pBuf, "  pd[packetId]->bytesPerRecord= %d\n", uRecBytes );
	DasBuf_printf(pBuf, "serialized: \n" );
	PktDesc_encode(pThis, pBuf);
	DasBuf_printf(pBuf, "plane#  offset length nitems dataType planeType \n" );
	
	int i;
	PlaneDesc* p = NULL;
	size_t uOffset = 0;
	for ( i=0; i<pThis->uPlanes; i++ ) {
		p= pThis->planes[i];
		DasEnc_toStr(p->pEncoding, sEncoding, 24);
		DasBuf_printf(pBuf, "%5d: %7d %6d %6d %s %s\n",
                i, uOffset, p->pEncoding->nWidth * p->uItems, p->uItems,
                sEncoding, PlaneType_toStr(p->planeType) );
		uOffset += p->pEncoding->nWidth * p->uItems;
    }
	
	fputs(pBuf->sBuf, stderr);
	del_DasBuf(pBuf);
}
