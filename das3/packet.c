/* Copyright (C) 2015-2026  Chris Piker <chris-piker@uiowa.edu>
 *               2004       Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <expat.h>

/* #include <das.h> */

#include "log.h"
#include "packet_p.h"
#include "enc_p.h"

static void _del_DasPkt(DasDesc* pDesc);  /* del_DasDesc() target */


/* ************************************************************************* */
/* Construction/Destruction */

DasPkt* new_DasPkt(){
	int iplane;
	DasPkt* pThis = (DasPkt*)calloc(1, sizeof(DasPkt));

	DasDesc_init((DasDesc*)pThis, DASDESCTYPE_PKT, _del_DasPkt);
 
	/* Redundant */
	for(iplane=0; iplane<DASPKT_MAX_PLANES; iplane++){
		pThis->planes[iplane]= NULL;
	}

	return pThis;
}

struct parse_pktdesc_stack{
 	 DasPkt* pd;
	 int nPktId;         /* for messages only, the packet keeps no ID */
	 
	 int errorCode;      /* error code */
    char errorMsg[DASXML_NAME_LEN];

    DasDesc* currentDesc;
};		

/* Formerly nested function "start" in parsePktDesc */
static void DasPkt_parseStart(void* data, const char* el, const char** attr) {

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
				DasDesc_setProp(pStack->currentDesc, sType, sName, attr[i+1] );
			}
			else{
				DasDesc_setProp(pStack->currentDesc, "String", attr[i], attr[i+1]);
			}
		}
		return;
	}

	if ( strcmp( el, "packet" )==0 ) {
		pStack->currentDesc = (DasDesc*)(pStack->pd);
		
		/* das2.2 <packet> has no attributes. */
		for (i=0; attr[i] != NULL; i+=2){
			daslog_warnf(
				"Packet %02d: attribute %s=\"%s\" is not part of das2.2 <packet>, ignored",
				pStack->nPktId, attr[i], attr[i+1]
			);
		}
		return;
	}

	/* This had better be a plane */
	if( strcmp( el, "x" ) !=0    && strcmp( el, "y" )!=0 && 
	    strcmp( el, "yscan" )!=0 && strcmp(el, "z")!=0){
		pStack->errorCode= DASERR_PKT;
		snprintf(pStack->errorMsg, DASXML_NAME_LEN - 1, 
		         "Unsupported tag in <packet> element: %s\n",el);
		return;		
	}
	
	das_planetype pt = dasplanetype_id(el);
	
	/* Use the keyword/value constructor for the plane object */
	DasPlane* pPlane = new_DasPlane_pairs((DasDesc*)pStack->pd, pt, attr);
	if(pPlane == NULL){
		pStack->errorCode= DASERR_PKT;
		snprintf(
			pStack->errorMsg,  DASXML_NAME_LEN - 1, 
			"Failed to plane at index %zu from packet %02d XML header",
			pStack->pd->uPlanes, pStack->nPktId
		);
		return;
	}
	pStack->pd->planes[pStack->pd->uPlanes] = pPlane;
	pStack->pd->uPlanes += 1;
	
	pStack->currentDesc = (DasDesc*)pPlane;
}

static void DasPkt_parseEnd(void *data, const char *el)
{	
	struct parse_pktdesc_stack* pStack = (struct parse_pktdesc_stack*)data;
	
	/* Plane is over, set current desc back to pkt desc */
	if( strcmp( el, "x" )==0  ||  strcmp( el, "y" )==0  ||  
	    strcmp( el, "yscan" )==0 || strcmp( el, "z" )==0) {
		  pStack->currentDesc = (DasDesc*)pStack->pd;
	 }
}

DasPkt* new_DasPkt_xml(DasBuf* pBuf, DasDesc* pParent, int nPktId)
{
	DasPkt* pThis = new_DasPkt();
	size_t uPos = DasBuf_readOffset(pBuf);
	
	struct parse_pktdesc_stack stack = {0};
	 
	stack.pd        = pThis;
	stack.nPktId    = nPktId;
	stack.errorCode = 0;    /* error code */
  
	XML_Parser p    = XML_ParserCreate("UTF-8"); 
	if ( !p ) {
		daserrf(DASERR_PKT, "Couldn't create XML parser\n" );
		return NULL;
	}
	XML_SetUserData(p, (void*)&stack);
	XML_SetElementHandler(p, DasPkt_parseStart, DasPkt_parseEnd);

	size_t uXml = 0;
	const char* sXml = (const char*)DasBuf_data(pBuf, &uXml);
	int nParRet = XML_Parse( p, sXml, (int)uXml, true );
	if(nParRet == 0){
		daserrf(DASERR_PKT, "Parse error at offset %ld:\n%s\n",
			XML_GetCurrentByteIndex(p),
			XML_ErrorString(XML_GetErrorCode(p))
		);
	}
	XML_ParserFree(p);

	if(stack.errorCode != 0){ 
		daserr(stack.errorCode, stack.errorMsg);
		del_DasDesc((DasDesc*)pThis);
		return NULL;
	}
    
	if(pThis->uPlanes == 0){
		DasBuf_setReadOffset(pBuf, uPos);
		daserrf(DASERR_PKT, "No data planes found! Here is the XML packet header:\n%s",
		           sXml ? sXml : "");
		del_DasDesc((DasDesc*)pThis);
		return NULL;
	}
	 
	pThis->base.parent = pParent;
	return pThis;
}


static void _del_DasPkt(DasDesc* pDesc){
	DasPkt* pThis = (DasPkt*)pDesc;
	size_t u = 0;
	for(u = 0; u < pThis->uPlanes; u++ ){
		del_DasDesc((DasDesc*)pThis->planes[u]);
	}
	
	DasDesc_freeProps(&(pThis->base));
	free(pThis);
}

/* ************************************************************************* */
/* Checking equality */
bool DasPkt_isSameFmt(const DasPkt* pPd1, const DasPkt* pPd2)
{
	DasPlane* pPlane1 = NULL;
	DasPlane* pPlane2 = NULL;
	
	if(pPd1->uPlanes != pPd2->uPlanes) return false;
	
	if(! DasDesc_isEqual((DasDesc*)pPd1, (DasDesc*)pPd2)) return false;
	
	for(size_t u = 0; u<pPd1->uPlanes; u++){
		pPlane1 = pPd1->planes[u];
		pPlane2 = pPd2->planes[u];
		if(pPlane1->planeType != pPlane2->planeType) return false;
		if(pPlane1->uItems != pPlane2->uItems) return false;
		if(! DasEnc_equals(pPlane1->pEncoding, pPlane2->pEncoding)) return false;
		if( strcmp(pPlane1->units, pPlane2->units) != 0) return false;
		
		if(pPlane1->planeType == DASPLANETYPE_YSCAN){
			if(! DasEnc_equals(pPlane1->pYEncoding, pPlane2->pYEncoding)) return false;
			if( strcmp(pPlane1->yTagUnits, pPlane2->yTagUnits) != 0) return false;
			
			if((pPlane1->pYTags != NULL)&&(pPlane2->pYTags == NULL)) return false;
			if((pPlane1->pYTags == NULL)&&(pPlane2->pYTags != NULL)) return false;

			if((pPlane1->pYTags != NULL)&&(pPlane2->pYTags != NULL)){
				for(size_t v = 0; v<pPlane1->uItems; v++)
					if(pPlane1->pYTags[v] != pPlane2->pYTags[v] ) return false;
			}
			
		}
		
		if(pPlane1->sName && !(pPlane2->sName)) return false;
		if(!(pPlane1->sName) && pPlane2->sName) return false;
		if(pPlane1->sName && pPlane2->sName && 
		   strcmp(pPlane1->sName, pPlane2->sName) != 0) return false;
		
		if(! DasDesc_isEqual((DasDesc*)pPlane1, (DasDesc*)pPlane2) ) return false;
	}
	
	return true;
}

/* ************************************************************************* */
/* Adding Sub-objects */

int DasPkt_addPlane(DasPkt* pThis, DasPlane* pPlane)
{
	if(pThis->uPlanes >= DASPKT_MAX_PLANES){
		daserrf(DASERR_PKT, "Too many planes, limit is %d\n", DASPKT_MAX_PLANES );
		return -1;
	}
	
	/* Check for legal patterns, don't want to mess up the client programs to bad */
	/* if(pPlane->planeType == X){
		if(DasPkt_numPlanesOfType(pThis, X) > 1){
			daserrf(DASERR_PKT, "There can only be a maximum of 2 X-planes in a packet");
			return -1;
		}
	}*/
	if(pPlane->planeType == DASPLANETYPE_YSCAN){
		if(DasPkt_numPlanesOfType(pThis, DASPLANETYPE_Z) > 0){
			daserrf(DASERR_PKT, "YScan and Z planes cannot be present in the same packet");
			return -1;
		}
	}
	if(pPlane->planeType == DASPLANETYPE_Z){
		if(DasPkt_numPlanesOfType(pThis, DASPLANETYPE_YSCAN) > 0){
			daserrf(DASERR_PKT, "Z and YScan planes cannot be present in the same packet");
			return -1;
		}
	}
	
	int iPos = pThis->uPlanes;
	pThis->uPlanes += 1;
	pThis->planes[iPos] = pPlane;
	
	pPlane->base.parent = (DasDesc*)pThis;
	
	return iPos;
}

das_errcode DasPkt_copyPlanes(DasPkt* pThis, const DasPkt* pOther)
{
	/* I can imagine merge scenarios, maybe this check should be removed... */
	if(pThis->uPlanes > 0)
		return daserrf(DASERR_PKT, "Can't use copyPlanes here, the packet already has "
		              "1 or more planes defined");
	
	/* put the new planes in the same order as the existing ones, but skip any
	 * gaps */
	DasPlane* pPlane = NULL;
	for(int i = 0; i < DASPKT_MAX_PLANES && i < pOther->uPlanes; i++){
		if(pOther->planes[i] == NULL){
			fprintf(stderr, "WARNING: Gap in plane definitions detected\n");
			continue;
		}
		
		pPlane = new_DasPlane_copy(pOther->planes[i]);
		if(DasPkt_addPlane(pThis, pPlane) == -1) return 18;
	}
	
	return 0;
}

bool DasPkt_validate(DasPkt* pThis ) {
   /* Make sure the dependent planes are present */
	if((DasPkt_numPlanesOfType(pThis, DASPLANETYPE_Y) > 0) && 
		(DasPkt_numPlanesOfType(pThis, DASPLANETYPE_X) == 0)){
		daserrf(DASERR_PKT, "In a packet header, Y planes are present without an X "
				     "plane");
		return false;
	}
	
	if((DasPkt_numPlanesOfType(pThis, DASPLANETYPE_YSCAN) > 0) && 
		(DasPkt_numPlanesOfType(pThis, DASPLANETYPE_X) == 0)){
		daserrf(DASERR_PKT, "In a packet header, YScan planes are present without an X "
				     "plane");
		return false;
	}
	
	if((DasPkt_numPlanesOfType(pThis, DASPLANETYPE_Z) > 0) && 
		(DasPkt_numPlanesOfType(pThis, DASPLANETYPE_Y) == 0)){
		daserrf(DASERR_PKT, "In a packet header, Z planes are present without a Y "
				     "plane");
		return false;
	}
	return true;
}

/* ************************************************************************** */
/* Convenience routines to cut client code bloat */

das_errcode DasPkt_setValAt(
	DasPkt* pThis, size_t uPlane, size_t uItem, double val
){
	DasPlane* pPlane = DasPkt_planeAt(pThis, uPlane);
	if(pPlane == NULL){ 
		return daserrf(DASERR_PKT, "Plane index %zu is not defined for this "
		                  "packet", uPlane);
	}
	
	return DasPlane_setValAt(pPlane, uItem, val);
}

das_errcode DasPkt_setVals(DasPkt* pThis, size_t uPlane, const double* pVals)
{
	DasPlane* pPlane = DasPkt_planeAt(pThis, uPlane);
	if(pPlane == NULL){ 
		return daserrf(DASERR_PKT, "Plane index %zu is not defined for this "
		                  "packet", uPlane);
	}
	DasPlane_setVals(pPlane, pVals);
	return 0;
}

/* ************************************************************************** */
/* Getting info and sub objects */



size_t DasPkt_numPlanesOfType(const DasPkt* pThis, das_planetype pt)
{
	size_t uPlanes = 0;
	for(int i = 0; i < pThis->uPlanes; i++)
		if(pThis->planes[i]->planeType == pt) uPlanes++;
	
	return uPlanes;
}

size_t DasPkt_numPlanes(const DasPkt* pThis){
	return pThis->uPlanes;
}

das_planetype DasPkt_planeTypeAt(const DasPkt* pThis, int iPlane)
{
	if(iPlane < 0 || iPlane >= DASPKT_MAX_PLANES)
		daserrf(DASERR_PKT, "ERROR: Invalid value for the plane index %d", iPlane);

	if(pThis->planes[iPlane] == NULL) 
		return DASPLANETYPE_INV;
	else
		return pThis->planes[iPlane]->planeType;
}

int DasPkt_planeIdxByName(DasPkt* pThis, const char * name, das_planetype planeType )
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

DasPlane* DasPkt_planeByName(DasPkt* pThis, const char* name)
{
	int iplane;	
	
	for(iplane=0; iplane<pThis->uPlanes; iplane++ ) {
		if(strcmp( name, pThis->planes[iplane]->sName )==0 ) {
			return DasPkt_planeAt(pThis, iplane );
		}
    }
    return NULL;
}

DasPlane* DasPkt_planeOfType(
	DasPkt* pThis, das_planetype ptype, int iRelIndex
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



int DasPkt_planeIdxOfType(
	const DasPkt* pThis, das_planetype ptype, int iRelIndex
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

DasPlane* DasPkt_xPlane(DasPkt* pThis ) {
	int iPlane = DasPkt_planeIdxOfType(pThis, DASPLANETYPE_X, 0);
	if(iPlane != -1) return pThis->planes[iPlane];
	return NULL;
}

DasPlane* DasPkt_planeAt(DasPkt* pThis, int iplane ) {
	if ( iplane<0 || iplane >= pThis->uPlanes ) {
		daserrf(DASERR_PKT, "invalid plane number, iplane=%d\n", iplane );
	}
	return pThis->planes[iplane];
}

int DasPkt_planeIdx(DasPkt* pThis, DasPlane* pPlane) {
	for(int u = 0; u < pThis->uPlanes; ++u){
		if(pThis->planes[u] == pPlane) return (int)u;
	}
	return -1;
}

DasPlane* DasPkt_replaceAt(DasPkt* pThis, int iPlane, DasPlane* pNew)
{
	if(iPlane < 0 || iPlane >= pThis->uPlanes){
		daserrf(DASERR_PKT, "Invalid plane number, iPlane = %d\n", iPlane);
   }

   DasPlane* pOld = pThis->planes[iPlane];
   if(pOld != NULL)
   	pOld->base.parent = NULL;

   pThis->planes[iPlane] = pNew;

   return pOld;
}

/* ************************************************************************* */
/* Encode Descriptor to XML */

das_errcode DasPkt_encodeHdr(const DasPkt* pThis, DasBuf* pBuf)
{
	das_errcode nRet = 18;
	
	nRet = DasBuf_printf(pBuf, "<packet>\n");
	if(nRet != 0) return nRet;
		
	if( (nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, "  ")) != 0) return nRet;
	
	int iplane;	
	
	for(iplane=0; iplane < pThis->uPlanes; iplane++ ) {
		switch(pThis->planes[iplane]->planeType){
		case DASPLANETYPE_X:
			nRet = DasPlane_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		case DASPLANETYPE_Y:
			nRet = DasPlane_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		case DASPLANETYPE_Z:
		case DASPLANETYPE_YSCAN:
			/* Z and YScan planes are always dependent data */
			nRet = DasPlane_encode(pThis->planes[iplane], pBuf, "  ");
			break;
		default:
			return daserrf(DASERR_PKT, "Code change detected in DasPkt_encodeHdr");
			break;
		}
		if(nRet != 0) return nRet;
    }
	return DasBuf_printf(pBuf, "</packet>\n");
}

/* ************************************************************************* */
/* Data I/O */

size_t DasPkt_recBytes(const DasPkt* pThis)
{
	size_t uRecBytes = 0;
	DasPlane* pPlane = NULL;
	for(size_t u = 0; u < pThis->uPlanes; u++){
		pPlane = pThis->planes[u];
		uRecBytes += pPlane->pEncoding->nWidth * pPlane->uItems;
	}
	return uRecBytes;
}

das_errcode DasPkt_decodeData(DasPkt* pThis, DasBuf* pBuf){
	
	size_t uRecBytes = DasPkt_recBytes(pThis);
	
	if(DasBuf_unread(pBuf) < uRecBytes){
		return daserrf(DASERR_PKT, "%zu bytes expected in each packet, only "
				            "received %zu", uRecBytes, 
				            DasBuf_unread(pBuf));
	}
	
	int nRet = 0;
	DasPlane* pPlane = NULL;
	for(int i = 0; i<pThis->uPlanes; i++){
		pPlane = pThis->planes[i];
		if( (nRet = DasPlane_decodeData(pPlane, pBuf)) != 0) break;
	}
	
	return nRet;
}


das_errcode DasPkt_encodeData(const DasPkt* pThis, DasBuf* pBuf){
	das_errcode nRet = 0;
	DasPlane* pPlane = NULL;
	bool bLast = false;
	
	/* check that everything get's written */
	size_t uBeg = DasBuf_written(pBuf);
	for(size_t u = 0; u < pThis->uPlanes; u++){
		pPlane = pThis->planes[u];
		if(u == pThis->uPlanes - 1) bLast = true;
		if( (nRet = DasPlane_encodeData(pPlane, pBuf, bLast)) != 0) return nRet;
	}
	
	/* TODO: Only run this check on the first pkt, saves un-needed calculations */
	size_t uEnd = DasBuf_written(pBuf);
	size_t uRecBytes = DasPkt_recBytes(pThis);
	if((uEnd - uBeg) != uRecBytes) 
		return daserrf(DASERR_PKT, "Partial packet written expected output %zu bytes, "
				            "wrote %zu bytes instead.", uEnd - uBeg, uRecBytes);
	return 0;
}

/* Nothing calls this, kept for debugging sessions.  DAS_DORMANT_CODE is never
   defined by default. */
#ifdef DAS_DORMANT_CODE
/* Diagnostic, dump to standard error */
static void DasPkt_dump(const DasPkt* pThis, FILE* file)
{
	DasBuf* pBuf = new_DasBuf(16000);
	char sEncoding[128] = {'\0'};
	
	size_t uRecBytes = DasPkt_recBytes(pThis);
	DasBuf_printf(pBuf, "  pd[packetId]->bytesPerRecord= %d\n", uRecBytes );
	DasBuf_printf(pBuf, "serialized: \n" );
	DasPkt_encodeHdr(pThis, pBuf);
	DasBuf_printf(pBuf, "plane#  offset length nitems dataType planeType \n" );
	
	int i;
	DasPlane* p = NULL;
	size_t uOffset = 0;
	for ( i=0; i<pThis->uPlanes; i++ ) {
		p= pThis->planes[i];
		DasEnc_toStr(p->pEncoding, sEncoding, 24);
		DasBuf_printf(pBuf, "%5d: %7d %6d %6d %s %s\n",
                i, uOffset, p->pEncoding->nWidth * p->uItems, p->uItems,
                sEncoding, dasplanetype_name(p->planeType) );
		uOffset += p->pEncoding->nWidth * p->uItems;
    }
	
	fputs(pBuf->sBuf, stderr);
	del_DasBuf(pBuf);
}
#endif /* DAS_DORMANT_CODE */
