/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* Data sources, see source.h.
 *
 * Failures over content, a broken node or a bad user value, are logged
 * and returned, never raised through das_error(), because this code
 * meets bad catalogs and typos every day and the default error
 * disposition would exit the program over either.  Clients that catch
 * log messages see everything; clients that do not still get verdicts.
 *
 * Grew out of the pilot in utilities/das_fetch.c, which carried the
 * translation rules for two node layouts.  Those rules are not written
 * down anywhere else yet; until the catalog spec states them, the
 * comments on _translate() are the reference.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

#include "util.h"
#include "log.h"
#include "time.h"
#include "http.h"
#include "node.h"
#include "node_priv.h"
#include "src_priv.h"

/* Section names as the current catalogs spell them.  Keys are built from
 * these; input accepts the singular forms too, see _das_src_secByName(). */
static const char* g_sSecName[das_src_sec_size] = {"coords", "data", "formats", "options"};

/* Scalars copied out of the document are short in practice, the longest
 * seen is a few dozen bytes; anything past this is cut rather than
 * carried. */
#define SCALAR_SZ 1024

/* ************************************************************************* */
/* Small string and JSON helpers.  DasJdo_get() takes slash paths, so the
 * dots in parameter names such as "read.time.min" are not special to it. */

static bool _strieq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return false;
	while(*a && *b){
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		++a; ++b;
	}
	return (*a == '\0') && (*b == '\0');
}

static void _strcpyz(char* sDest, const char* sSrc, size_t uDest)
{
	if(uDest == 0) return;
	strncpy(sDest, sSrc ? sSrc : "", uDest - 1);
	sDest[uDest - 1] = '\0';
}

static const DasJdo* _jget(const DasJdo* p, const char* sPath)
{
	return ((p == NULL)||(sPath == NULL)) ? NULL : DasJdo_get(p, sPath);
}

static const char* _jstr(const DasJdo* p, const char* sPath)
{
	const DasJdo* pSub = _jget(p, sPath);
	return ((pSub != NULL)&&(pSub->type == das_json_type_str)) ? DasJdo_string(pSub) : NULL;
}

static bool _jisdict(const DasJdo* p){ return (p != NULL)&&(p->type == das_json_type_dict); }
static bool _jisary(const DasJdo* p){  return (p != NULL)&&(p->type == das_json_type_ary); }
static bool _jtrue(const DasJdo* p){   return (p != NULL)&&(p->type == das_json_type_true); }
static bool _jisbool(const DasJdo* p){
	return (p != NULL)&&((p->type == das_json_type_true)||(p->type == das_json_type_false));
}
static size_t _jlen(const DasJdo* p)
{
	if(_jisdict(p)) return ((const das_json_dict*)p->value)->length;
	if(_jisary(p))  return ((const das_json_ary*)p->value)->length;
	return 0;
}

/* das_strdup() returns NULL for "", which here would turn an empty default
   into no default.  Empty strings are copied as empty strings. */
static char* _strdupz(const char* s)
{
	if((s == NULL)||(s[0] == '\0')) return (char*)calloc(1, 1);
	return das_strdup(s);
}

/* Every string a source keeps, keys, default values, enum items and range
 * ends, is its own small allocation, freed with the source.  A possible
 * optimization, not taken: the source owns pDoc for its whole life, so
 * plain JSON strings could be referenced in place and only numbers, which
 * need formatting, copied.  That would remove most of these allocations
 * at the cost of a copied-or-borrowed flag per string.  A full string
 * arena was considered and rejected: it needs an exact size pass that
 * duplicates the formatting logic below, and it hides overruns from
 * valgrind.  Revisit if a heap profile of a browser build points here. */
static char* _scalarDup(const DasJdo* p)
{
	char sBuf[SCALAR_SZ];
	if(!_das_json_scalar(p, sBuf, sizeof(sBuf))) return NULL;
	return _strdupz(sBuf);
}

static bool _parseBool(const char* s, bool* pOut)
{
	if(s == NULL) return false;
	if(_strieq(s,"true")||_strieq(s,"yes")||_strieq(s,"on")||_strieq(s,"1")||
	   _strieq(s,"t")||_strieq(s,"y")){ *pOut = true; return true; }
	if(_strieq(s,"false")||_strieq(s,"no")||_strieq(s,"off")||_strieq(s,"0")||
	   _strieq(s,"f")||_strieq(s,"n")){ *pOut = false; return true; }
	return false;
}

/* snprintf that appends and never runs the cursor past the buffer */
static size_t _catf(char* sBuf, size_t uLen, size_t u, const char* sFmt, ...)
{
	if(u >= uLen) return u;
	va_list ap;
	va_start(ap, sFmt);
	int n = vsnprintf(sBuf + u, uLen - u, sFmt, ap);
	va_end(ap);
	if(n < 0) return u;
	return (u + (size_t)n < uLen) ? u + (size_t)n : uLen;
}

/* ************************************************************************* */
/* Key grammar */

const char* das_src_sec_name(das_src_sec_e sec)
{
	return ((sec >= 0)&&(sec < das_src_sec_size)) ? g_sSecName[sec] : "";
}

bool _das_src_secByName(const char* s, das_src_sec_e* pSec)
{
	if(_strieq(s,"coord")||_strieq(s,"coords")||_strieq(s,"coordinate")||_strieq(s,"coordinates")){
		*pSec = das_src_sec_coord; return true;
	}
	if(_strieq(s,"data")){ *pSec = das_src_sec_data; return true; }
	if(_strieq(s,"format")||_strieq(s,"formats")||_strieq(s,"fmt")){
		*pSec = das_src_sec_format; return true;
	}
	if(_strieq(s,"option")||_strieq(s,"options")||_strieq(s,"opt")||_strieq(s,"opts")){
		*pSec = das_src_sec_option; return true;
	}
	return false;
}

const char* _das_src_propName(const char* s)
{
	if(s == NULL) return "";
	if(_strieq(s, "minimum")||_strieq(s, "min"))        return "min";
	if(_strieq(s, "maximum")||_strieq(s, "max"))        return "max";
	if(_strieq(s, "resolution")||_strieq(s, "res"))     return "res";
	if(_strieq(s, "interval")||_strieq(s, "inter"))     return "inter";
	if(_strieq(s, "enable")||_strieq(s, "enabled"))     return "enabled";
	return s;
}

bool _das_src_propEq(const char* a, const char* b)
{
	return _strieq(_das_src_propName(a), _das_src_propName(b));
}

int _das_src_splitKey(const char* sKey, char aParts[3][DAS_MAX_ID_BUFSZ])
{
	if(sKey == NULL) return 0;
	int n = 0;
	const char* pBeg = sKey;
	while(true){
		const char* pEnd = strchr(pBeg, '.');
		size_t uLen = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		if((uLen == 0)||(uLen >= DAS_MAX_ID_BUFSZ)||(n == 3)) return 0;
		memcpy(aParts[n], pBeg, uLen);
		aParts[n][uLen] = '\0';
		++n;
		if(pEnd == NULL) break;
		pBeg = pEnd + 1;
	}
	return n;
}

/* ************************************************************************* */
/* Parsing the interface */

/* Fold a type word from a node to the enum.  NULL and unknown words are
 * text, the caller decides whether that is final. */
static bool _typeByName(const char* s, das_srcprop_type_e* pType)
{
	if(s == NULL) return false;
	if(_strieq(s,"boolean")||_strieq(s,"bool")){ *pType = das_srcprop_bool; return true; }
	if(_strieq(s,"integer")||_strieq(s,"int")){ *pType = das_srcprop_int; return true; }
	if(_strieq(s,"real")||_strieq(s,"double")||_strieq(s,"float")){
		*pType = das_srcprop_real; return true;
	}
	if(_strieq(s,"isotime")||_strieq(s,"time")||_strieq(s,"datetime")){
		*pType = das_srcprop_time; return true;
	}
	if(_strieq(s,"enum")){ *pType = das_srcprop_enum; return true; }
	if(_strieq(s,"string")||_strieq(s,"text")){ *pType = das_srcprop_text; return true; }
	return false;
}

/* Preference order: a set enum list, the property's own type, a boolean
 * default, a flag toggle, the protocol parameter's type, the shape of the
 * value, then text.  The enum list goes first because a property declared
 * "string" with a list of choices is still a list of choices to anyone
 * presenting it.  A flag set parameter type is a server side packing
 * detail and says nothing about the single toggle that feeds it. */
static void _resolveType(DasSrcProp* p)
{
	p->sTypeName = _jstr(p->pJdo, "type");
	if(p->sTypeName == NULL) p->sTypeName = "";
	const DasSrcCtrl* pC = p->pCtrl;
	const DasJdo* pVal = _jget(p->pJdo, "value");

	if(pC && (pC->uEnum > 0)){ p->type = das_srcprop_enum; return; }
	if(_typeByName(p->sTypeName, &(p->type))){
		if(p->type != das_srcprop_enum) return;
	}
	if(_jisbool(pVal) || (pC && (_jstr(pC->pSet, "flag") != NULL))){
		p->type = das_srcprop_bool; return;
	}
	const char* sParamType = pC ? _jstr(pC->pParam, "type") : NULL;
	if(_typeByName(sParamType, &(p->type)) && (p->type != das_srcprop_enum)){
		if(p->sTypeName[0] == '\0') p->sTypeName = sParamType;
		return;
	}
	if((pVal != NULL)&&(pVal->type == das_json_type_num)){ p->type = das_srcprop_real; return; }
	p->type = das_srcprop_text;
}

/* Enum entries are usually dicts carrying value plus per item pval or flag
 * overrides, but a bare scalar list is accepted too. */
static bool _fillEnum(DasSrcCtrl* p)
{
	const DasJdo* pEnum = _jget(p->pSet, "enum");
	if(!_jisary(pEnum)) return true;

	size_t uMax = _jlen(pEnum);
	if(uMax == 0) return true;
	p->aEnum = (char**)calloc(uMax, sizeof(char*));
	if(p->aEnum == NULL) return false;

	for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
		const DasJdo* pVal = _jisdict(pEl->value) ? _jget(pEl->value, "value") : pEl->value;
		char* s = _scalarDup(pVal);
		if(s == NULL) continue;
		p->aEnum[p->uEnum] = s;
		p->uEnum += 1;
	}
	return true;
}

static void _fillRange(DasSrcCtrl* p)
{
	const DasSrcProp* pProp = p->pProp;
	const DasJdo* pRange = _jget(p->pSet, "range");
	if(!_jisary(pRange)) pRange = _jget(p->pParam, "range");
	if(!_jisary(pRange) && (pProp->sec == das_src_sec_coord) &&
	   (_das_src_propEq(pProp->sName, "min") || _das_src_propEq(pProp->sName, "max")))
		pRange = _jget(pProp->pGroup->pJdo, "validRange");
	if(!_jisary(pRange)) return;

	const das_json_ary_el* pEl = DasJdo_aryFirst(pRange);
	if(pEl == NULL) return;
	p->sLo = _scalarDup(pEl->value);
	if(pEl->next) p->sHi = _scalarDup(pEl->next->value);
}

/* Release what a control owns; the control itself lives in aCtrl */
static void _clearCtrl(DasSrcCtrl* p)
{
	if(p == NULL) return;
	for(size_t v = 0; v < p->uEnum; ++v) free(p->aEnum[v]);
	free(p->aEnum);
	free(p->sLo);
	free(p->sHi);
}

/* A member of a group dict is a property if it is a dict that looks like
 * one.  In the current layout every member of "props" qualifies; in the
 * 2018 layout properties sit beside the group's own name and title
 * strings, and a units dict with a value counts as the units property
 * just as it does under props today. */
static bool _isPropDict(const DasJdo* p)
{
	if(!_jisdict(p)) return false;
	return (_jget(p, "value") != NULL)||(_jget(p, "set") != NULL)||(_jget(p, "type") != NULL);
}

static const DasJdo* _groupProps(const DasJdo* pGroup)
{
	const DasJdo* pProps = _jget(pGroup, "props");
	return _jisdict(pProps) ? pProps : pGroup;
}

/* Count and fill share one walk.  With pSrc NULL only the counts are
 * wanted. */
typedef struct {
	size_t uGroup; size_t uProp; size_t uCtrl;
	DasSrc* pSrc; const DasJdo* pParams; bool bFail;
} _walk_t;

/* A property is settable when its set block names a protocol parameter.
 * The counting pass and the fill pass must agree on this, so it is one
 * function. */
static const DasJdo* _setBlock(const DasJdo* pJdo)
{
	const DasJdo* pSet = _jget(pJdo, "set");
	return (_jisdict(pSet) && (_jstr(pSet, "param") != NULL)) ? pSet : NULL;
}

static void _walkProp(_walk_t* pW, DasSrcGroup* pG, const char* sName, const DasJdo* pJdo)
{
	if(pW->pSrc == NULL){
		pW->uProp += 1;
		if(_setBlock(pJdo) != NULL) pW->uCtrl += 1;
		return;
	}
	if(pW->bFail) return;

	DasSrc* pS = pW->pSrc;
	DasSrcProp* p = pS->aProp + pW->uProp;
	memset(p, 0, sizeof(DasSrcProp));
	p->sec = pG->sec;
	p->pGroup = pG;
	p->sName = sName;
	p->pJdo = pJdo;

	size_t uKey = strlen(g_sSecName[pG->sec]) + strlen(pG->sName) + strlen(sName) + 3;
	p->sKey = (char*)malloc(uKey);
	if(p->sKey == NULL){ pW->bFail = true; return; }
	if(pG->sName[0] == '\0')
		snprintf(p->sKey, uKey, "%s.%s", g_sSecName[pG->sec], sName);
	else
		snprintf(p->sKey, uKey, "%s.%s.%s", g_sSecName[pG->sec], pG->sName, sName);

	p->sValue = _scalarDup(_jget(pJdo, "value"));

	const DasJdo* pSet = _jget(pJdo, "set");
	if(_jisdict(pSet)){
		const char* sParam = _jstr(pSet, "param");
		if(sParam == NULL){
			daslog_warn_v("Property %s has a set block that names no parameter, "
			              "treating it as fixed", p->sKey);
		}
		else{
			/* Controls live in one block sized by the counting pass */
			if(pW->uCtrl >= pS->uCtrl){ pW->bFail = true; return; }
			DasSrcCtrl* pC = pS->aCtrl + pW->uCtrl;
			pW->uCtrl += 1;
			memset(pC, 0, sizeof(DasSrcCtrl));
			pC->pProp = p;
			pC->pSet = pSet;
			pC->sParam = sParam;
			pC->pParam = _jget(pW->pParams, sParam);
			pC->bRequired = _jtrue(_jget(pSet, "required")) || _jtrue(_jget(pC->pParam, "required"));
			p->pCtrl = pC;
			if(!_fillEnum(pC)){ pW->bFail = true; return; }
			_fillRange(pC);
		}
	}
	_resolveType(p);

	if(_das_src_propEq(sName, "enabled") && (pG->nEnable < 0)){
		pG->nEnable = (int)(pW->uProp - pG->uFirst);
		pG->sXor = _jstr(pJdo, "xorGroup");
	}
	pW->uProp += 1;
	pG->uCount += 1;
}

static void _walkGroup(_walk_t* pW, das_src_sec_e sec, const char* sName, const DasJdo* pJdo)
{
	if(!_jisdict(pJdo)) return;
	DasSrcGroup* pG = NULL;
	if(pW->pSrc != NULL){
		pG = pW->pSrc->aGroup + pW->uGroup;
		memset(pG, 0, sizeof(DasSrcGroup));
		pG->pSrc = pW->pSrc;
		pG->sec = sec;
		pG->sName = sName;
		pG->pJdo = pJdo;
		pG->uFirst = pW->uProp;
		pG->nEnable = -1;

		const DasJdo* pMime = _jget(pJdo, "mimeTypes");
		if(_jisary(pMime) && (_jlen(pMime) > 0)){
			pG->aMime = (const char**)calloc(_jlen(pMime), sizeof(char*));
			if(pG->aMime == NULL){ pW->bFail = true; return; }
			for(const das_json_ary_el* pEl = DasJdo_aryFirst(pMime); pEl; pEl = pEl->next){
				if(DasJdo_string(pEl->value) == NULL) continue;
				pG->aMime[pG->uMime] = DasJdo_string(pEl->value);
				pG->uMime += 1;
			}
		}
		pW->pSrc->aSecCount[sec] += 1;
	}
	pW->uGroup += 1;

	const DasJdo* pProps = _groupProps(pJdo);
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pProps); pEl; pEl = pEl->next){
		if(!_isPropDict(pEl->value)) continue;
		_walkProp(pW, pG, pEl->name->string, pEl->value);
	}
}

/* Groups of a section in presentation order: "time" first for coordinates,
 * otherwise as the node lists them.  Forms want time on top and nodes do
 * not always put it there. */
static void _walkSection(_walk_t* pW, das_src_sec_e sec, const DasJdo* pSec)
{
	if(!_jisdict(pSec)) return;
	if(pW->pSrc != NULL) pW->pSrc->aSecFirst[sec] = pW->uGroup;
	if(sec == das_src_sec_coord){
		for(const das_json_dict_el* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next)
			if(_strieq(pEl->name->string, "time")) _walkGroup(pW, sec, pEl->name->string, pEl->value);
	}
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next){
		if((sec == das_src_sec_coord) && _strieq(pEl->name->string, "time")) continue;
		_walkGroup(pW, sec, pEl->name->string, pEl->value);
	}
}

static void _walkIface(_walk_t* pW, const DasJdo* pIface)
{
	const DasJdo* pCoords = _jget(pIface, "coords");
	if(pCoords == NULL) pCoords = _jget(pIface, "coordinates");
	_walkSection(pW, das_src_sec_coord, pCoords);
	_walkSection(pW, das_src_sec_data, _jget(pIface, "data"));
	_walkSection(pW, das_src_sec_format, _jget(pIface, "formats"));

	/* Options are one group with no enclosing section */
	const DasJdo* pOpts = _jget(pIface, "options");
	if(_jisdict(pOpts)){
		if(pW->pSrc != NULL) pW->pSrc->aSecFirst[das_src_sec_option] = pW->uGroup;
		_walkGroup(pW, das_src_sec_option, "", pOpts);
	}
}

/* ************************************************************************* */
/* Examples, parsed into requisitions when the source is built */

/* "coords/time/props/min" -> "coords.time.min".  The section token is
 * folded to the current spelling so a saved requisition reads the same
 * whatever node it came from. */
static bool _exPathToKey(const char* sPath, char* sKey, size_t uLen)
{
	size_t u = 0;
	int nTok = 0;
	const char* pBeg = sPath;
	sKey[0] = '\0';
	while(*pBeg){
		const char* pEnd = strchr(pBeg, '/');
		size_t n = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		char sTok[DAS_MAX_ID_BUFSZ];
		if((n == 0)||(n >= sizeof(sTok))) return false;
		memcpy(sTok, pBeg, n); sTok[n] = '\0';
		const char* sOut = sTok;
		das_src_sec_e sec;
		if(strcmp(sTok, "props") == 0) sOut = NULL;
		else if((nTok == 0) && _das_src_secByName(sTok, &sec)) sOut = g_sSecName[sec];
		if(sOut != NULL){
			if(u + strlen(sOut) + 2 >= uLen) return false;
			u = _catf(sKey, uLen, u, "%s%s", nTok ? "." : "", sOut);
			++nTok;
		}
		if(pEnd == NULL) break;
		pBeg = pEnd + 1;
	}
	return nTok > 0;
}

/* The flag value a control sends, without any prefix: the parameter's
 * flags entry for it, or the flag name itself */
static const char* _ctrlFlagVal(const DasSrcCtrl* pC, const char* sFlag, char* sBuf, size_t uLen)
{
	char sPath[DAS_MAX_ID_BUFSZ + 8];
	snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
	const DasJdo* pF = _jget(pC->pParam, sPath);
	if(!_das_json_scalar(_jget(pF, "value"), sBuf, uLen)) _strcpyz(sBuf, sFlag, uLen);
	return sBuf;
}

/* A 2018 example is raw protocol parameters.  Each is mapped back onto
 * the control that would have produced it, so the requisition looks the
 * same as one from a current node.  Parameters no control explains are
 * kept as raw parameters, nothing is dropped. */
static bool _exampleFromParams(DasSrc* pThis, DasReq* pReq, const DasJdo* pParams)
{
	char sVal[SCALAR_SZ], sFlagVal[SCALAR_SZ], sPVal[SCALAR_SZ];
	for(const das_json_dict_el* pP = DasJdo_dictFirst(pParams); pP; pP = pP->next){
		const char* sName = pP->name->string;
		if(!_das_json_scalar(pP->value, sVal, sizeof(sVal))) continue;

		/* the controls feeding this parameter */
		bool bFlagSet = false, bHandled = false;
		for(size_t u = 0; u < pThis->uProp; ++u){
			const DasSrcCtrl* pC = pThis->aProp[u].pCtrl;
			if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;
			if(_jstr(pC->pSet, "flag") != NULL){ bFlagSet = true; break; }
		}

		if(bFlagSet){
			const char* sSep = NULL;
			for(size_t u = 0; (sSep == NULL) && (u < pThis->uProp); ++u){
				const DasSrcCtrl* pC = pThis->aProp[u].pCtrl;
				if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;
				sSep = _jstr(pC->pParam, "flagSep");
				if(sSep == NULL) sSep = _jstr(pC->pParam, "flag_sep");
			}
			if((sSep == NULL)||(sSep[0] == '\0')) sSep = " ";

			char sLeft[SCALAR_SZ] = {'\0'};   /* tokens no control owns */
			char* pTok = sVal;
			while(pTok && *pTok){
				char* pNext = strstr(pTok, sSep);
				if(pNext){ *pNext = '\0'; pNext += strlen(sSep); }
				bool bFound = false;
				for(size_t u = 0; !bFound && (u < pThis->uProp); ++u){
					const DasSrcCtrl* pC = pThis->aProp[u].pCtrl;
					if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;
					const char* sFlag = _jstr(pC->pSet, "flag");
					if(sFlag == NULL) continue;
					_ctrlFlagVal(pC, sFlag, sFlagVal, sizeof(sFlagVal));
					if(_strieq(sFlagVal, pTok)){
						if(DasReq_set(pReq, pThis->aProp[u].sKey, "true") != DAS_OKAY) return false;
						bFound = true;
					}
				}
				if(!bFound && (pTok[0] != '\0')){
					size_t n = strlen(sLeft);
					snprintf(sLeft + n, sizeof(sLeft) - n, "%s%s", n ? sSep : "", pTok);
				}
				pTok = pNext;
			}
			if(sLeft[0] != '\0')
				if(DasReq_setRaw(pReq, sName, sLeft) != DAS_OKAY) return false;
			continue;
		}

		for(size_t u = 0; !bHandled && (u < pThis->uProp); ++u){
			const DasSrcProp* p = pThis->aProp + u;
			const DasSrcCtrl* pC = p->pCtrl;
			if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;

			const DasJdo* pEnum = _jget(pC->pSet, "enum");
			if(_jisary(pEnum)){
				/* which choice sends this protocol value? */
				for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); !bHandled && pEl; pEl = pEl->next){
					const DasJdo* pItem = _jisdict(pEl->value) ? _jget(pEl->value, "value") : pEl->value;
					char sItem[SCALAR_SZ];
					if(!_das_json_scalar(pItem, sItem, sizeof(sItem))) continue;
					bool bHit = _das_json_scalar(_jget(pEl->value, "pval"), sPVal, sizeof(sPVal)) ?
					            _strieq(sPVal, sVal) : _strieq(sItem, sVal);
					if(bHit){
						if(DasReq_set(pReq, p->sKey, sItem) != DAS_OKAY) return false;
						bHandled = true;
					}
				}
				continue;
			}
			if(_das_json_scalar(_jget(pC->pSet, "pval"), sPVal, sizeof(sPVal))){
				/* a fixed protocol value stands for one state of a toggle */
				if(_strieq(sPVal, sVal)){
					bool bOn = true;
					const DasJdo* pTrig = _jget(pC->pSet, "value");
					if(_jisbool(pTrig)) bOn = _jtrue(pTrig);
					if(DasReq_set(pReq, p->sKey, bOn ? "true" : "false") != DAS_OKAY) return false;
					bHandled = true;
				}
				continue;
			}
			if(DasReq_set(pReq, p->sKey, sVal) != DAS_OKAY) return false;
			bHandled = true;
		}
		if(!bHandled)
			if(DasReq_setRaw(pReq, sName, sVal) != DAS_OKAY) return false;
	}
	return true;
}

static bool _loadExamples(DasSrc* pThis)
{
	const DasJdo* pEx = _jget(pThis->pIface, "examples");
	const DasJdo* pProto = _jget(pThis->pDoc, "protocol");
	bool bLegacyEx = false;
	if(!_jisary(pEx)){
		pEx = _jget(pProto, "examples");
		bLegacyEx = _jisdict(pEx);
		if(!bLegacyEx) return true;
	}
	size_t uMax = _jlen(pEx);
	if(uMax == 0) return true;
	pThis->aExample = (DasReq**)calloc(uMax, sizeof(DasReq*));
	if(pThis->aExample == NULL) return false;

	char sKey[256], sVal[SCALAR_SZ];
	if(bLegacyEx){
		for(const das_json_dict_el* pEl = DasJdo_dictFirst(pEx); pEl; pEl = pEl->next){
			DasReq* pReq = new_DasReq();
			if(pReq == NULL) return false;
			pThis->aExample[pThis->uExample] = pReq;
			pThis->uExample += 1;
			const char* sLabel = _jstr(pEl->value, "title");
			if(sLabel == NULL) sLabel = _jstr(pEl->value, "name");
			if(sLabel == NULL) sLabel = pEl->name->string;
			DasReq_setLabel(pReq, sLabel);
			DasSrc_stamp(pThis, pReq);
			const DasJdo* pParams = _jget(pEl->value, "http_params");
			if(_jisdict(pParams) && !_exampleFromParams(pThis, pReq, pParams)) return false;
		}
		return true;
	}

	for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEx); pEl; pEl = pEl->next){
		DasReq* pReq = new_DasReq();
		if(pReq == NULL) return false;
		pThis->aExample[pThis->uExample] = pReq;
		pThis->uExample += 1;
		const char* sLabel = _jstr(pEl->value, "label");
		if(sLabel == NULL) sLabel = _jstr(pEl->value, "title");
		if(sLabel == NULL) sLabel = _jstr(pEl->value, "name");
		DasReq_setLabel(pReq, sLabel);
		DasSrc_stamp(pThis, pReq);
		const DasJdo* pSettings = _jget(pEl->value, "settings");
		for(const das_json_dict_el* pP = DasJdo_dictFirst(pSettings); pP; pP = pP->next){
			if(!_exPathToKey(pP->name->string, sKey, sizeof(sKey))){
				daslog_warn_v("Ignoring example setting '%s' in %s, the path makes no sense",
				              pP->name->string, pThis->sUrl);
				continue;
			}
			if(!_das_json_scalar(pP->value, sVal, sizeof(sVal))) continue;
			if(DasReq_set(pReq, sKey, sVal) != DAS_OKAY) return false;
		}
	}
	return true;
}

/* ************************************************************************* */
/* Life cycle */

void del_DasSrc(DasSrc* pThis)
{
	if(pThis == NULL) return;
	for(size_t u = 0; u < pThis->uExample; ++u) del_DasReq(pThis->aExample[u]);
	free(pThis->aExample);
	for(size_t u = 0; u < pThis->uProp; ++u){
		free(pThis->aProp[u].sKey);
		free(pThis->aProp[u].sValue);
		_clearCtrl(pThis->aProp[u].pCtrl);
	}
	free(pThis->aProp);
	free(pThis->aCtrl);
	for(size_t u = 0; u < pThis->uGroup; ++u) free(pThis->aGroup[u].aMime);
	free(pThis->aGroup);
	free(pThis->pDoc);
	free(pThis->sPath);
	free(pThis->sUrl);
	free(pThis);
}

DasSrc* new_DasSrc_json(const char* sDoc, size_t uLen, const char* sPathUri, const char* sUrl)
{
	const char* sWhere = sUrl ? sUrl : (sPathUri ? sPathUri : "(memory)");
	DasJdo* pDoc = das_json_parse(sDoc, uLen);
	if(!_jisdict(pDoc)){
		daslog_error_v("Source document %s is not a JSON object", sWhere);
		free(pDoc);
		return NULL;
	}
	const char* sType = _jstr(pDoc, "type");
	if(!_strieq(sType, "HttpStreamSrc")){
		daslog_error_v("Node %s is of type %s, only HttpStreamSrc nodes describe data sources "
		               "this library can query", sWhere, sType ? sType : "(none)");
		free(pDoc);
		return NULL;
	}

	DasStreamSrc* pStream = (DasStreamSrc*)calloc(1, sizeof(DasStreamSrc));
	if(pStream == NULL){ free(pDoc); return NULL; }
	DasSrc* pThis = &(pStream->base);
	pThis->kind = das_src_kind_stream;
	pThis->pDoc = pDoc;

	/* Every source has a URI, see D2URI_LOCAL.  One from a bare document
	 * with no location is "local:/memory", which is all that can be said */
	char sUri[600];
	if((sPathUri != NULL) && (sPathUri[0] != '\0'))
		_das_uri_expand(sPathUri, sUri, sizeof(sUri));
	else if((sUrl != NULL) && (sUrl[0] != '\0'))
		_das_uri_local(sUrl, sUri, sizeof(sUri));
	else
		strcpy(sUri, D2URI_LOCAL "/memory");
	pThis->sPath = _strdupz(sUri);
	pThis->sUrl = _strdupz(sUrl);
	if((pThis->sPath == NULL)||(pThis->sUrl == NULL)){ del_DasSrc(pThis); return NULL; }

	pThis->pIface = _jget(pDoc, "interface");
	if(!_jisdict(pThis->pIface)) pThis->pIface = NULL;

	pStream->pProto = _jget(pDoc, "protocol");
	if(!_jisdict(pStream->pProto)) pStream->pProto = NULL;
	pStream->pParams = _jget(pStream->pProto, "httpParams");
	if(pStream->pParams == NULL) pStream->pParams = _jget(pStream->pProto, "http_params");
	pStream->pBaseUrls = _jget(pStream->pProto, "baseUrls");
	if(pStream->pBaseUrls == NULL) pStream->pBaseUrls = _jget(pStream->pProto, "base_urls");
	if(!_jisary(pStream->pBaseUrls)) pStream->pBaseUrls = NULL;
	pThis->bLegacy = (_jget(pStream->pProto, "http_params") != NULL) ||
	                 (_jget(pThis->pIface, "coordinates") != NULL);

	/* Two passes over the same walk: count, allocate exactly that many,
	 * fill.  Groups, properties and controls are three blocks, so a source
	 * is a handful of allocations however large its interface, which
	 * matters on a heap that never shrinks, as in a browser build. */
	_walk_t w = {0, 0, 0, NULL, pStream->pParams, false};
	_walkIface(&w, pThis->pIface);
	pThis->aGroup = (DasSrcGroup*)calloc(w.uGroup ? w.uGroup : 1, sizeof(DasSrcGroup));
	pThis->aProp = (DasSrcProp*)calloc(w.uProp ? w.uProp : 1, sizeof(DasSrcProp));
	pThis->aCtrl = (DasSrcCtrl*)calloc(w.uCtrl ? w.uCtrl : 1, sizeof(DasSrcCtrl));
	if((pThis->aGroup == NULL)||(pThis->aProp == NULL)||(pThis->aCtrl == NULL)){
		del_DasSrc(pThis);
		return NULL;
	}
	size_t uGroups = w.uGroup, uProps = w.uProp, uCtrls = w.uCtrl;
	pThis->uCtrl = uCtrls;   /* the fill pass checks against it */

	w.uGroup = 0; w.uProp = 0; w.uCtrl = 0; w.pSrc = pThis;
	_walkIface(&w, pThis->pIface);
	pThis->uGroup = w.uGroup;
	pThis->uProp = w.uProp;
	pThis->uCtrl = w.uCtrl;
	if(w.bFail || (uGroups != w.uGroup) || (uProps != w.uProp) || (uCtrls != w.uCtrl)){
		daslog_error_v("Couldn't build the interface of source %s", sWhere);
		del_DasSrc(pThis);
		return NULL;
	}

	if(!_loadExamples(pThis)){
		daslog_error_v("Couldn't read the examples of source %s", sWhere);
		del_DasSrc(pThis);
		return NULL;
	}
	return pThis;
}

DasSrc* new_DasSrc_node(const DasNode* pNode)
{
	if(pNode == NULL) return NULL;
	if(!DasNode_isStreamSrc(pNode)){
		/* Name the children so a caller can pick one; picking is not done
		   here because the library has no basis for the choice. */
		const char* aNames[8];
		size_t uKids = dassrc_loadable(pNode, aNames, 8);
		char sMsg[512];
		size_t n = snprintf(sMsg, sizeof(sMsg), "Node %s is not a data source", DasNode_srcUrl(pNode));
		if(uKids > 0) n = _catf(sMsg, sizeof(sMsg), n, ", its loadable children are:");
		for(size_t u = 0; (u < uKids) && (u < 8); ++u) n = _catf(sMsg, sizeof(sMsg), n, " %s", aNames[u]);
		daslog_error_v("%s", sMsg);
		return NULL;
	}
	const DasJdo* pDom = DasNode_getJdo(pNode, NULL);
	if(pDom == NULL) return NULL;

	/* A serialized copy keeps the source independent of the tree.  Nodes
	   are a few KB, so this is cheaper than sharing with a reference count
	   would be to get right. */
	size_t uLen = 0;
	char* sText = (char*)DasJdo_writeMinified(pDom, &uLen);
	if(sText == NULL){
		daslog_error_v("Couldn't copy the document of node %s", DasNode_srcUrl(pNode));
		return NULL;
	}
	DasSrc* pThis = new_DasSrc_json(sText, uLen, DasNode_pathUri(pNode), DasNode_srcUrl(pNode));
	free(sText);
	return pThis;
}

/* ************************************************************************* */
/* Identity */

das_src_kind_e DasSrc_kind(const DasSrc* pThis){ return pThis->kind; }
const char* DasSrc_path(const DasSrc* pThis){ return pThis->sPath; }
const char* DasSrc_url(const DasSrc* pThis){ return pThis->sUrl; }

static const char* _docStr(const DasSrc* pThis, const char* sPath)
{
	const char* s = _jstr(pThis->pDoc, sPath);
	return s ? s : "";
}

const char* DasSrc_name(const DasSrc* pThis)
{
	const char* s = _jstr(pThis->pDoc, "label");
	if(s == NULL) s = _jstr(pThis->pDoc, "name");
	return s ? s : "";
}

const char* DasSrc_title(const DasSrc* pThis){ return _docStr(pThis, "title"); }
const char* DasSrc_version(const DasSrc* pThis){ return _docStr(pThis, "version"); }

bool DasSrc_needsAuth(const DasSrc* pThis)
{
	const DasJdo* pProto = _jget(pThis->pDoc, "protocol");
	const DasJdo* pAuth = _jget(pProto, "authorization");
	if(pAuth == NULL) pAuth = _jget(pProto, "authentication");
	return _jtrue(_jget(pAuth, "required"));
}

static const DasJdo* _contacts(const DasSrc* pThis)
{
	const DasJdo* p = _jget(pThis->pDoc, "contacts");
	if(!_jisary(p)) p = _jget(pThis->pDoc, "tech_contacts");
	return _jisary(p) ? p : NULL;
}

size_t DasSrc_contactCount(const DasSrc* pThis){ return _jlen(_contacts(pThis)); }

const char* DasSrc_contact(const DasSrc* pThis, size_t uIdx, const char** psEmail)
{
	const das_json_ary_el* pEl = DasJdo_aryFirst(_contacts(pThis));
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	if(pEl == NULL) return NULL;
	const char* sName = _jstr(pEl->value, "name");
	const char* sEmail = _jstr(pEl->value, "email");
	if(psEmail) *psEmail = sEmail ? sEmail : "";
	return sName ? sName : "";
}

const DasJdo* DasSrc_jdo(const DasSrc* pThis, const char* sFragment)
{
	if((sFragment == NULL)||(sFragment[0] == '\0')) return pThis->pDoc;
	return DasJdo_get(pThis->pDoc, sFragment);
}

/* ************************************************************************* */
/* Groups */

size_t DasSrc_groupCount(const DasSrc* pThis, das_src_sec_e sec)
{
	return ((sec >= 0)&&(sec < das_src_sec_size)) ? pThis->aSecCount[sec] : 0;
}

const DasSrcGroup* DasSrc_group(const DasSrc* pThis, das_src_sec_e sec, size_t uIdx)
{
	if(uIdx >= DasSrc_groupCount(pThis, sec)) return NULL;
	return pThis->aGroup + pThis->aSecFirst[sec] + uIdx;
}

const DasSrcGroup* DasSrc_findGroup(const DasSrc* pThis, das_src_sec_e sec, const char* sName)
{
	if(sName == NULL) sName = "";
	for(size_t u = 0; u < DasSrc_groupCount(pThis, sec); ++u){
		const DasSrcGroup* pG = DasSrc_group(pThis, sec, u);
		if(_strieq(pG->sName, sName)) return pG;
	}
	return NULL;
}

das_src_sec_e DasSrcGroup_section(const DasSrcGroup* pThis){ return pThis->sec; }
const char* DasSrcGroup_name(const DasSrcGroup* pThis){ return pThis->sName; }

const char* DasSrcGroup_label(const DasSrcGroup* pThis)
{
	const char* s = _jstr(pThis->pJdo, "label");
	if(s == NULL) s = _jstr(pThis->pJdo, "name");
	return s ? s : pThis->sName;
}

const char* DasSrcGroup_title(const DasSrcGroup* pThis)
{
	const char* s = _jstr(pThis->pJdo, "title");
	return s ? s : "";
}

const DasSrcProp* DasSrcGroup_prop(const DasSrcGroup* pThis, size_t uIdx)
{
	return (uIdx < pThis->uCount) ? pThis->pSrc->aProp + pThis->uFirst + uIdx : NULL;
}

const DasSrcProp* DasSrcGroup_enable(const DasSrcGroup* pThis)
{
	return (pThis->nEnable < 0) ? NULL : DasSrcGroup_prop(pThis, (size_t)pThis->nEnable);
}

const char* DasSrcGroup_xorGroup(const DasSrcGroup* pThis){ return pThis->sXor; }
size_t DasSrcGroup_mimeCount(const DasSrcGroup* pThis){ return pThis->uMime; }

const char* DasSrcGroup_mime(const DasSrcGroup* pThis, size_t uIdx)
{
	return (uIdx < pThis->uMime) ? pThis->aMime[uIdx] : NULL;
}

size_t DasSrcGroup_propCount(const DasSrcGroup* pThis){ return pThis->uCount; }

const DasSrcProp* DasSrcGroup_findProp(const DasSrcGroup* pThis, const char* sName)
{
	for(size_t u = 0; u < pThis->uCount; ++u){
		const DasSrcProp* p = DasSrcGroup_prop(pThis, u);
		if(_das_src_propEq(p->sName, sName)) return p;
	}
	return NULL;
}

size_t DasSrcGroup_ctrlCount(const DasSrcGroup* pThis)
{
	size_t n = 0;
	for(size_t u = 0; u < pThis->uCount; ++u)
		if(DasSrcGroup_prop(pThis, u)->pCtrl != NULL) ++n;
	return n;
}

/* ************************************************************************* */
/* Properties */

size_t DasSrc_propCount(const DasSrc* pThis){ return pThis->uProp; }

const DasSrcProp* DasSrc_prop(const DasSrc* pThis, size_t uIdx)
{
	return (uIdx < pThis->uProp) ? pThis->aProp + uIdx : NULL;
}

das_src_sec_e DasSrcProp_section(const DasSrcProp* pThis){ return pThis->sec; }
const DasSrcGroup* DasSrcProp_group(const DasSrcProp* pThis){ return pThis->pGroup; }
const char* DasSrcProp_name(const DasSrcProp* pThis){ return pThis->sName; }
const char* DasSrcProp_key(const DasSrcProp* pThis){ return pThis->sKey; }
das_srcprop_type_e DasSrcProp_type(const DasSrcProp* pThis){ return pThis->type; }
const char* DasSrcProp_typeName(const DasSrcProp* pThis){ return pThis->sTypeName; }
const char* DasSrcProp_value(const DasSrcProp* pThis){ return pThis->sValue; }
const DasSrcCtrl* DasSrcProp_ctrl(const DasSrcProp* pThis){ return pThis->pCtrl; }

/* Units for display.  Current nodes put a units string on the property;
 * both layouts may carry a units property with a "value" at the group
 * level. */
const char* DasSrcProp_units(const DasSrcProp* pThis)
{
	const DasJdo* pU = _jget(pThis->pJdo, "units");
	if(pU != NULL){
		if(pU->type == das_json_type_str) return DasJdo_string(pU);
		if(_jstr(pU, "value")) return _jstr(pU, "value");
	}
	if(pThis->sec == das_src_sec_coord){
		const char* s = _jstr(_groupProps(pThis->pGroup->pJdo), "units/value");
		if(s != NULL) return s;
	}
	if(pThis->type == das_srcprop_time) return "UTC";
	return "";
}

const char* DasSrcProp_title(const DasSrcProp* pThis, char* sBuf, size_t uLen)
{
	const char* s = _jstr(pThis->pJdo, "title");
	if(s == NULL) s = _jstr(pThis->pJdo, "summary");
	if(s == NULL) s = _jstr(pThis->pJdo, "label");
	if(s == NULL) s = _jstr(pThis->pJdo, "name");
	if(s != NULL) return s;
	if(pThis->sec == das_src_sec_option) return "";

	const char* sGroup = DasSrcGroup_label(pThis->pGroup);
	const char* sProp = _das_src_propName(pThis->sName);
	if(strcmp(sProp, "enabled") == 0){
		if(pThis->sec == das_src_sec_format){
			snprintf(sBuf, uLen, "Output as %s", sGroup);
		}
		else{
			/* A variable's title is usually the physical quantity, which
			   reads better after "Output" than its short label does */
			const char* sTitle = _jstr(pThis->pGroup->pJdo, "title");
			snprintf(sBuf, uLen, "Output %s", sTitle ? sTitle : sGroup);
		}
		return sBuf;
	}
	if(pThis->sec == das_src_sec_coord){
		const char* sFmt = NULL;
		if(strcmp(sProp, "min") == 0)        sFmt = "Minimum %s value to output";
		else if(strcmp(sProp, "max") == 0)   sFmt = "Maximum %s value to output";
		else if(strcmp(sProp, "res") == 0)   sFmt = "Average over %s bins of this width";
		else if(strcmp(sProp, "inter") == 0) sFmt = "Step between output %s values";
		else if(strcmp(sProp, "units") == 0) sFmt = "Units of %s values";
		if(sFmt != NULL){
			snprintf(sBuf, uLen, sFmt, sGroup);
			return sBuf;
		}
	}
	snprintf(sBuf, uLen, "%s %s", sGroup, pThis->sName);
	return sBuf;
}

/* ************************************************************************* */
/* Controls */

const DasSrcProp* DasSrcCtrl_prop(const DasSrcCtrl* pThis){ return pThis->pProp; }
bool DasSrcCtrl_required(const DasSrcCtrl* pThis){ return pThis->bRequired; }
const char* DasSrcCtrl_param(const DasSrcCtrl* pThis){ return pThis->sParam; }
size_t DasSrcCtrl_enumCount(const DasSrcCtrl* pThis){ return pThis->uEnum; }

const char* DasSrcCtrl_enum(const DasSrcCtrl* pThis, size_t uIdx)
{
	return (uIdx < pThis->uEnum) ? pThis->aEnum[uIdx] : NULL;
}

bool DasSrcCtrl_range(const DasSrcCtrl* pThis, const char** psLo, const char** psHi)
{
	if((pThis->sLo == NULL)&&(pThis->sHi == NULL)) return false;
	if(psLo) *psLo = pThis->sLo ? pThis->sLo : "";
	if(psHi) *psHi = pThis->sHi ? pThis->sHi : "";
	return true;
}

DasErrCode DasSrcCtrl_check(const DasSrcCtrl* pThis, const char* sVal, char* sWhy, size_t uWhy)
{
	if(sVal == NULL) sVal = "";
	if(sWhy && uWhy) sWhy[0] = '\0';
	char sDummy[1];
	if(sWhy == NULL){ sWhy = sDummy; uWhy = 1; }
	const char* sKey = pThis->pProp->sKey;
	char* pEnd = NULL;

	switch(pThis->pProp->type){
	case das_srcprop_bool:{
		bool b;
		if(_parseBool(sVal, &b)) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a boolean value for %s, use true or false", sVal, sKey);
		return DASERR_SRC;
	}
	case das_srcprop_enum:{
		for(size_t u = 0; u < pThis->uEnum; ++u)
			if(_strieq(pThis->aEnum[u], sVal)) return DAS_OKAY;
		size_t u = snprintf(sWhy, uWhy, "'%s' is not an allowed value for %s, choose one of:", sVal, sKey);
		for(size_t v = 0; v < pThis->uEnum; ++v)
			u = _catf(sWhy, uWhy, u, "%s %s", v ? "," : "", pThis->aEnum[v]);
		return DASERR_SRC;
	}
	case das_srcprop_int:
		strtol(sVal, &pEnd, 10);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not an integer value for %s", sVal, sKey);
		return DASERR_SRC;
	case das_srcprop_real:
		strtod(sVal, &pEnd);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a real value for %s", sVal, sKey);
		return DASERR_SRC;
	case das_srcprop_time:{
		/* "now" is how nodes spell an open ended range and servers take
		   it in requests too */
		das_time dt;
		if(_strieq(sVal, "now") || dt_parsetime(&dt, sVal)) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a time value for %s, use ISO 8601 such as "
		         "2026-01-15T12:00", sVal, sKey);
		return DASERR_SRC;
	}
	default:
		return DAS_OKAY;
	}
}

/* ************************************************************************* */
/* Key lookup */

int _DasSrc_match(const DasSrc* pThis, const char* sKey, const DasSrcProp** aOut, int nMax)
{
	char aParts[3][DAS_MAX_ID_BUFSZ];
	int nParts = _das_src_splitKey(sKey, aParts);
	if(nParts == 0) return 0;

	int n = 0;
	das_src_sec_e sec;
	const DasSrcProp* p = NULL;

#define _ADD_MATCH if(n < nMax) aOut[n] = p; ++n;

	if(nParts == 3){
		if(!_das_src_secByName(aParts[0], &sec)) return 0;
		for(size_t u = 0; u < pThis->uProp; ++u){
			p = pThis->aProp + u;
			if((p->sec == sec) && _strieq(p->pGroup->sName, aParts[1]) &&
			   _das_src_propEq(p->sName, aParts[2])){ _ADD_MATCH }
		}
		return n;
	}

	if(nParts == 2){
		/* "option.X" is a section qualified option, "SEC.GROUP" toggles a
		   variable, anything else is GROUP.PROP in any section */
		if(_das_src_secByName(aParts[0], &sec)){
			for(size_t u = 0; u < pThis->uProp; ++u){
				p = pThis->aProp + u;
				if(p->sec != sec) continue;
				if((sec == das_src_sec_option) && _das_src_propEq(p->sName, aParts[1])){ _ADD_MATCH }
				else if((sec != das_src_sec_option) && _strieq(p->pGroup->sName, aParts[1]) &&
				        _das_src_propEq(p->sName, "enabled")){ _ADD_MATCH }
			}
			if(n > 0) return n;
		}
		for(size_t u = 0; u < pThis->uProp; ++u){
			p = pThis->aProp + u;
			if(_strieq(p->pGroup->sName, aParts[0]) && _das_src_propEq(p->sName, aParts[1])){ _ADD_MATCH }
		}
		return n;
	}

	/* One part: an option, or a variable/format toggle */
	for(size_t u = 0; u < pThis->uProp; ++u){
		p = pThis->aProp + u;
		if((p->sec == das_src_sec_option) && _das_src_propEq(p->sName, aParts[0])){ _ADD_MATCH }
		else if((p->sec != das_src_sec_option) && _strieq(p->pGroup->sName, aParts[0]) &&
		        _das_src_propEq(p->sName, "enabled")){ _ADD_MATCH }
	}
	if(n > 0) return n;

	/* Last resort, a bare property name.  Unique in practice for things
	   like 'serial', ambiguous for 'fracSecs' shared by das and csv, and
	   the ambiguity message then lists the full keys. */
	for(size_t u = 0; u < pThis->uProp; ++u){
		p = pThis->aProp + u;
		if((p->sec != das_src_sec_option) && _das_src_propEq(p->sName, aParts[0])){ _ADD_MATCH }
	}
#undef _ADD_MATCH
	return n;
}

const DasSrcGroup* _DasSrc_coordGroup(const DasSrc* pThis, const char* sKey)
{
	char aParts[3][DAS_MAX_ID_BUFSZ];
	int nParts = _das_src_splitKey(sKey, aParts);
	const char* sGroup = NULL;
	das_src_sec_e sec;
	if(nParts == 1) sGroup = aParts[0];
	else if((nParts == 2) && _das_src_secByName(aParts[0], &sec) && (sec == das_src_sec_coord))
		sGroup = aParts[1];
	else return NULL;
	return DasSrc_findGroup(pThis, das_src_sec_coord, sGroup);
}

#define MAX_MATCH 8

static void _ambiguous(const char* sKey, const DasSrcProp** aMatch, int n, char* sWhy, size_t uWhy)
{
	size_t u = snprintf(sWhy, uWhy, "Key '%s' is ambiguous, use one of:", sKey);
	for(int i = 0; (i < n) && (i < MAX_MATCH); ++i)
		u = _catf(sWhy, uWhy, u, " %s", aMatch[i]->sKey);
}

const DasSrcProp* DasSrc_findProp(const DasSrc* pThis, const char* sKey, char* sWhy, size_t uWhy)
{
	char sDummy[1];
	if(sWhy == NULL){ sWhy = sDummy; uWhy = 1; }
	sWhy[0] = '\0';
	const DasSrcProp* aMatch[MAX_MATCH];
	int n = _DasSrc_match(pThis, sKey, aMatch, MAX_MATCH);
	if(n == 1) return aMatch[0];
	if(n == 0) snprintf(sWhy, uWhy, "No property named '%s' in this source", sKey);
	else _ambiguous(sKey, aMatch, n, sWhy, uWhy);
	return NULL;
}

/* ************************************************************************* */
/* Requisitions */

size_t DasSrc_exampleCount(const DasSrc* pThis){ return pThis->uExample; }

const char* DasSrc_exampleLabel(const DasSrc* pThis, size_t uIdx)
{
	return (uIdx < pThis->uExample) ? DasReq_label(pThis->aExample[uIdx]) : NULL;
}

void DasSrc_stamp(const DasSrc* pThis, DasReq* pReq)
{
	DasReq_setSrc(pReq, pThis->sPath, pThis->sUrl, DasSrc_version(pThis));
}

DasReq* DasSrc_newReq(const DasSrc* pThis, int nExample)
{
	if(nExample >= 0){
		if((size_t)nExample >= pThis->uExample){
			daslog_error_v("Source %s has %zu example(s), there is no example %d",
			               pThis->sUrl, pThis->uExample, nExample);
			return NULL;
		}
		return copy_DasReq(pThis->aExample[nExample]);
	}
	DasReq* pReq = new_DasReq();
	if(pReq == NULL) return NULL;
	DasSrc_stamp(pThis, pReq);
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		if((p->pCtrl == NULL)||!p->pCtrl->bRequired||(p->sValue != NULL)) continue;
		if(DasReq_set(pReq, p->sKey, "") != DAS_OKAY){ del_DasReq(pReq); return NULL; }
	}
	return pReq;
}

/* Identity is the catalog URI alone.  The URL is where one copy of the
 * node happened to come from; a mirror or a local mount of the same node
 * has a different one and must validate the same requisitions. */
static bool _stampMatches(const DasSrc* pThis, const DasReq* pReq)
{
	const char* sPath = DasReq_srcPath(pReq);
	if(sPath[0] == '\0') return true;   /* unstamped */
	return _strieq(sPath, pThis->sPath);
}

/* One item of a coordinate range: find the control, check the value,
 * add the derived entry.  Returns false with sWhy filled on any trouble. */
static bool _rangeItem(
	const DasSrc* pThis, DasReq* pReq, const DasSrcGroup* pG, const char* sProp,
	const char* sVal, bool* aPresent, bool* aCovered, char* sWhy, size_t uWhy
){
	const DasSrcProp* p = DasSrcGroup_findProp(pG, sProp);
	if((p == NULL)||(p->pCtrl == NULL)){
		snprintf(sWhy, uWhy, "Coordinate %s has no settable '%s' property", pG->sName, sProp);
		return false;
	}
	if(DasSrcCtrl_check(p->pCtrl, sVal, sWhy, uWhy) != DAS_OKAY) return false;
	int n = _DasReq_add(pReq, p->sKey, sVal, true);
	if(n < 0){ snprintf(sWhy, uWhy, "Out of memory"); return false; }
	size_t uProp = (size_t)(p - pThis->aProp);
	pReq->aEntry[n].nProp = (int)uProp;
	_DasReq_setVerdict(pReq, (size_t)n, das_req_ok, NULL);
	aPresent[uProp] = true;
	aCovered[uProp] = true;
	return true;
}

/* GROUP=MIN,MAX[,STEP] or GROUP=SPAN, see "Keys" in source.h */
static bool _rangeEntry(
	const DasSrc* pThis, DasReq* pReq, const DasSrcGroup* pG, const char* sVal,
	bool* aPresent, bool* aCovered, char* sWhy, size_t uWhy
){
	if(strchr(sVal, ',') == NULL){
		/* a single time denotes its span */
		const DasSrcProp* pMin = DasSrcGroup_findProp(pG, "min");
		if((pMin == NULL)||(pMin->type != das_srcprop_time)){
			snprintf(sWhy, uWhy, "Give coordinate %s a range as MIN,MAX", pG->sName);
			return false;
		}
		das_time dtBeg, dtEnd;
		if(!dt_parsespan(sVal, &dtBeg, &dtEnd)){
			snprintf(sWhy, uWhy, "'%s' is neither a MIN,MAX range nor a time span such "
			         "as 2026-01-02 or 2026-01-02T14", sVal);
			return false;
		}
		char sBeg[40], sEnd[40];
		dt_isoc(&dtBeg, sBeg, sizeof(sBeg), 0);
		dt_isoc(&dtEnd, sEnd, sizeof(sEnd), 0);
		return _rangeItem(pThis, pReq, pG, "min", sBeg, aPresent, aCovered, sWhy, uWhy) &&
		       _rangeItem(pThis, pReq, pG, "max", sEnd, aPresent, aCovered, sWhy, uWhy);
	}

	const char* aProps[3] = {"min", "max", NULL};
	aProps[2] = DasSrcGroup_findProp(pG, "res") ? "res" : "inter";

	char sCopy[SCALAR_SZ];
	_strcpyz(sCopy, sVal, sizeof(sCopy));
	char* pTok = sCopy;
	for(int i = 0; i < 4; ++i){
		char* pComma = strchr(pTok, ',');
		if(pComma) *pComma = '\0';
		if(i == 3){
			snprintf(sWhy, uWhy, "Too many values for coordinate %s, at most MIN,MAX,STEP", pG->sName);
			return false;
		}
		if(pTok[0] != '\0')
			if(!_rangeItem(pThis, pReq, pG, aProps[i], pTok, aPresent, aCovered, sWhy, uWhy))
				return false;
		if(pComma == NULL) break;
		pTok = pComma + 1;
	}
	return true;
}

DasErrCode DasSrc_check(const DasSrc* pThis, DasReq* pReq, size_t* puBad)
{
	if(puBad) *puBad = 0;
	if(!_stampMatches(pThis, pReq)){
		daslog_error_v("Requisition is for source %s%s, not %s%s",
		               DasReq_srcPath(pReq), DasReq_srcPath(pReq)[0] ? "" : DasReq_srcUrl(pReq),
		               pThis->sPath, pThis->sPath[0] ? "" : pThis->sUrl);
		return DASERR_SRC;
	}
	_DasReq_dropDerived(pReq);
	_DasReq_clearVerdicts(pReq);

	/* present: some entry names the property, even badly.  covered: a good
	   non-empty value was given for it. */
	size_t uProp = pThis->uProp ? pThis->uProp : 1;
	bool* aPresent = (bool*)calloc(uProp * 2, sizeof(bool));
	if(aPresent == NULL) return DASERR_SRC;
	bool* aCovered = aPresent + uProp;

	size_t uBad = 0;
	char sWhy[1024];
	size_t uClient = pReq->uEntry;   /* derived entries are appended past here */
	for(size_t u = 0; u < uClient; ++u){
		const char* sKey = pReq->aEntry[u].sKey;
		const char* sVal = pReq->aEntry[u].sVal;

		const DasSrcGroup* pG = _DasSrc_coordGroup(pThis, sKey);
		if(pG != NULL){
			if(_rangeEntry(pThis, pReq, pG, sVal, aPresent, aCovered, sWhy, sizeof(sWhy)))
				_DasReq_setVerdict(pReq, u, das_req_ok, NULL);
			else{
				_DasReq_setVerdict(pReq, u, das_req_bad, sWhy);
				++uBad;
			}
			continue;
		}

		const DasSrcProp* aMatch[MAX_MATCH];
		int n = _DasSrc_match(pThis, sKey, aMatch, MAX_MATCH);
		if(n == 0){
			snprintf(sWhy, sizeof(sWhy), "No property named '%s' in this source", sKey);
			_DasReq_setVerdict(pReq, u, das_req_bad, sWhy); ++uBad; continue;
		}
		if(n > 1){
			_ambiguous(sKey, aMatch, n, sWhy, sizeof(sWhy));
			_DasReq_setVerdict(pReq, u, das_req_bad, sWhy); ++uBad; continue;
		}
		const DasSrcProp* p = aMatch[0];
		size_t uP = (size_t)(p - pThis->aProp);
		pReq->aEntry[u].nProp = (int)uP;
		if(p->pCtrl == NULL){
			snprintf(sWhy, sizeof(sWhy), "%s is a fixed property of this source and cannot be set", p->sKey);
			_DasReq_setVerdict(pReq, u, das_req_bad, sWhy); ++uBad; continue;
		}
		aPresent[uP] = true;
		if(sVal[0] == '\0'){
			if(p->pCtrl->bRequired && (p->sValue == NULL)){
				snprintf(sWhy, sizeof(sWhy), "%s is required, give it a value", p->sKey);
				_DasReq_setVerdict(pReq, u, das_req_bad, sWhy); ++uBad;
			}
			else
				_DasReq_setVerdict(pReq, u, das_req_ok, NULL);
			continue;
		}
		if(DasSrcCtrl_check(p->pCtrl, sVal, sWhy, sizeof(sWhy)) != DAS_OKAY){
			_DasReq_setVerdict(pReq, u, das_req_bad, sWhy); ++uBad; continue;
		}
		aCovered[uP] = true;
		_DasReq_setVerdict(pReq, u, das_req_ok, NULL);
	}

	/* An empty entry for a required control is fine after all when a range
	   entry covered the same control: the blank requisition lists
	   coords.time.min, the user typed time=A,B, both name one property */
	for(size_t u = 0; u < uClient; ++u){
		struct das_req_entry* pE = pReq->aEntry + u;
		if((pE->verdict != das_req_bad)||(pE->nProp < 0)||(pE->sVal[0] != '\0')) continue;
		if(!aCovered[pE->nProp]) continue;
		_DasReq_setVerdict(pReq, u, das_req_ok, NULL);
		--uBad;
	}

	/* required controls nobody mentioned */
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		if((p->pCtrl == NULL)||!p->pCtrl->bRequired||(p->sValue != NULL)||aPresent[u]) continue;
		int n = _DasReq_add(pReq, p->sKey, "", true);
		if(n < 0) break;
		pReq->aEntry[n].nProp = (int)u;
		snprintf(sWhy, sizeof(sWhy), "%s is required, set it with %s=VALUE", p->sKey, p->sKey);
		_DasReq_setVerdict(pReq, (size_t)n, das_req_bad, sWhy);
		++uBad;
	}
	free(aPresent);
	if(puBad) *puBad = uBad;
	return uBad ? DASERR_SRC : DAS_OKAY;
}

/* The format group a requisition turns on, else the node's default */
static const DasSrcGroup* _chosenFormat(const DasSrc* pThis, const DasReq* pReq)
{
	const DasSrcGroup* pDef = NULL;
	for(size_t g = 0; g < DasSrc_groupCount(pThis, das_src_sec_format); ++g){
		const DasSrcGroup* pG = DasSrc_group(pThis, das_src_sec_format, g);
		const DasSrcProp* pOn = DasSrcGroup_enable(pG);
		if(pOn == NULL) continue;
		bool b = false;
		if((pDef == NULL) && _parseBool(pOn->sValue, &b) && b) pDef = pG;
		if(pReq == NULL) continue;
		for(size_t u = 0; u < pReq->uEntry; ++u){
			if(pReq->aEntry[u].bDerived) continue;
			const DasSrcProp* aMatch[1];
			if(_DasSrc_match(pThis, pReq->aEntry[u].sKey, aMatch, 1) != 1) continue;
			if((aMatch[0] == pOn) && _parseBool(pReq->aEntry[u].sVal, &b) && b) return pG;
		}
	}
	return pDef;
}

const char* DasSrc_mime(const DasSrc* pThis, const DasReq* pReq)
{
	const DasSrcGroup* pG = _chosenFormat(pThis, pReq);
	if((pG != NULL) && (pG->uMime > 0)) return pG->aMime[0];

	/* 2018 nodes state a default format outside the interface */
	const char* s = _jstr(pThis->pDoc, "format/default/mime");
	if(s == NULL) s = _jstr(pThis->pDoc, "protocol/format/default/mime");
	if(s == NULL) s = _jstr(pThis->pDoc, "interface/format/default/mime");
	return s ? s : "";
}

/* ************************************************************************* */
/* Stream sources: translating a requisition to protocol parameters */

static const DasStreamSrc* _stream(const DasSrc* pThis)
{
	return (pThis->kind == das_src_kind_stream) ? (const DasStreamSrc*)pThis : NULL;
}

size_t DasStreamSrc_urlCount(const DasSrc* pThis)
{
	const DasStreamSrc* pS = _stream(pThis);
	return pS ? _jlen(pS->pBaseUrls) : 0;
}

const char* DasStreamSrc_baseUrl(const DasSrc* pThis, size_t uIdx)
{
	const DasStreamSrc* pS = _stream(pThis);
	if(pS == NULL) return NULL;
	const das_json_ary_el* pEl = DasJdo_aryFirst(pS->pBaseUrls);
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	return pEl ? DasJdo_string(pEl->value) : NULL;
}

/* The output parameter list.  Names are borrowed from the document or the
 * requisition, values are owned because flag sets grow by appending. */
typedef struct { const char* sName; char* sVal; } _param_t;
typedef struct { _param_t* a; size_t u; size_t uCap; } _out_t;

static _param_t* _outFind(_out_t* pO, const char* sName)
{
	for(size_t u = 0; u < pO->u; ++u)
		if(strcmp(pO->a[u].sName, sName) == 0) return pO->a + u;
	return NULL;
}

static bool _outSet(_out_t* pO, const char* sName, const char* sVal)
{
	_param_t* p = _outFind(pO, sName);
	if(p == NULL){
		if(pO->u == pO->uCap){
			size_t uCap = pO->uCap ? pO->uCap * 2 : 16;
			_param_t* a = (_param_t*)realloc(pO->a, uCap * sizeof(_param_t));
			if(a == NULL) return false;
			pO->a = a;
			pO->uCap = uCap;
		}
		p = pO->a + pO->u;
		p->sName = sName;
		p->sVal = NULL;
		pO->u += 1;
	}
	char* sNew = _strdupz(sVal);
	if(sNew == NULL) return false;
	free(p->sVal);
	p->sVal = sNew;
	return true;
}

static bool _outAddFlag(_out_t* pO, const char* sName, const char* sFlagVal, const char* sSep)
{
	_param_t* p = _outFind(pO, sName);
	if((p == NULL)||(p->sVal[0] == '\0')) return _outSet(pO, sName, sFlagVal);
	char* sNew = das_string("%s%s%s", p->sVal, sSep, sFlagVal);
	if(sNew == NULL) return false;
	free(p->sVal);
	p->sVal = sNew;
	return true;
}

static void _outClear(_out_t* pO)
{
	for(size_t u = 0; u < pO->u; ++u) free(pO->a[u].sVal);
	free(pO->a);
	memset(pO, 0, sizeof(_out_t));
}

/* One control's value to protocol parameters, by its set block:
 *
 *   - enum: the chosen item's "pval" if it has one, else its value, and
 *     an item may name its own flag
 *   - boolean: the block fires for one state only, "value" (true unless
 *     said otherwise); the other state is the default and sends nothing.
 *     With neither pval nor flag the state itself is sent as true/false.
 *   - flag: the flag's value (its entry under the parameter's flags, else
 *     the flag name), with the flag's prefix, appended to the parameter
 *     with the parameter's flagSep
 *   - otherwise the value as given, or the block's pval
 */
static bool _translate(const DasSrcCtrl* pC, const char* sVal, _out_t* pO, char* sWhy, size_t uWhy)
{
	const DasSrcProp* p = pC->pProp;
	char sFlag[DAS_MAX_ID_BUFSZ] = {'\0'};
	char sPVal[SCALAR_SZ] = {'\0'};
	_strcpyz(sFlag, _jstr(pC->pSet, "flag"), sizeof(sFlag));
	bool bHavePVal = _das_json_scalar(_jget(pC->pSet, "pval"), sPVal, sizeof(sPVal));

	const DasJdo* pEnum = _jget(pC->pSet, "enum");
	if(_jisary(pEnum)){
		char sItem[SCALAR_SZ];
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
			const DasJdo* pItem = _jisdict(pEl->value) ? _jget(pEl->value, "value") : pEl->value;
			if(!_das_json_scalar(pItem, sItem, sizeof(sItem))) continue;
			if(!_strieq(sItem, sVal)) continue;
			if(_das_json_scalar(_jget(pEl->value, "pval"), sPVal, sizeof(sPVal))) bHavePVal = true;
			else if(!bHavePVal){ _strcpyz(sPVal, sItem, sizeof(sPVal)); bHavePVal = true; }
			if(_jstr(pEl->value, "flag")) _strcpyz(sFlag, _jstr(pEl->value, "flag"), sizeof(sFlag));
			break;
		}
	}
	else if(p->type == das_srcprop_bool){
		bool bUser = false;
		_parseBool(sVal, &bUser);
		const DasJdo* pTrig = _jget(pC->pSet, "value");
		if(_jisbool(pTrig) && (bUser != _jtrue(pTrig))) return true;
		if(!bHavePVal && (sFlag[0] == '\0')){
			_strcpyz(sPVal, bUser ? "true" : "false", sizeof(sPVal));
			bHavePVal = true;
		}
	}

	if(sFlag[0] != '\0'){
		char sPath[DAS_MAX_ID_BUFSZ + 8];
		snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
		const DasJdo* pF = _jget(pC->pParam, sPath);
		if(pF == NULL){
			snprintf(sWhy, uWhy, "Flag '%s' for %s is not defined under protocol parameter %s, "
			         "the node is broken", sFlag, p->sKey, pC->sParam);
			return false;
		}
		char sFVal[SCALAR_SZ], sFull[SCALAR_SZ];
		if(!_das_json_scalar(_jget(pF, "value"), sFVal, sizeof(sFVal))) _strcpyz(sFVal, sFlag, sizeof(sFVal));
		const char* sPrefix = _jstr(pF, "prefix");
		snprintf(sFull, sizeof(sFull), "%s%s", sPrefix ? sPrefix : "", sFVal);
		const char* sSep = _jstr(pC->pParam, "flagSep");
		if(sSep == NULL) sSep = _jstr(pC->pParam, "flag_sep");
		if(sSep == NULL) sSep = " ";
		if(!_outAddFlag(pO, pC->sParam, sFull, sSep)){ snprintf(sWhy, uWhy, "Out of memory"); return false; }
		return true;
	}
	if(!_outSet(pO, pC->sParam, bHavePVal ? sPVal : sVal)){ snprintf(sWhy, uWhy, "Out of memory"); return false; }
	return true;
}

/* The value a requisition gives a property: the last good non-empty
 * entry that resolved to it, derived or not */
static const char* _reqValueFor(const DasReq* pReq, size_t uProp)
{
	const char* sVal = NULL;
	for(size_t u = 0; u < pReq->uEntry; ++u){
		const struct das_req_entry* pE = pReq->aEntry + u;
		if((pE->nProp != (int)uProp)||(pE->verdict != das_req_ok)||(pE->sVal[0] == '\0')) continue;
		sVal = pE->sVal;
	}
	return sVal;
}

static bool _sameAsDefault(const DasSrcProp* p, const char* sVal)
{
	if(p->sValue == NULL) return false;
	if(p->type == das_srcprop_bool){
		bool bA = false, bB = false;
		return _parseBool(sVal, &bA) && _parseBool(p->sValue, &bB) && (bA == bB);
	}
	return _strieq(sVal, p->sValue);
}

size_t DasStreamSrc_url(const DasSrc* pThis, DasReq* pReq, size_t uBase, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	const DasStreamSrc* pS = _stream(pThis);
	if((pS == NULL)||(pS->pProto == NULL)||(pS->pBaseUrls == NULL)){
		daslog_error_v("Source %s has no protocol section with base URLs, it cannot be queried",
		               pThis->sUrl);
		return 0;
	}
	const char* sBase = DasStreamSrc_baseUrl(pThis, uBase);
	if(sBase == NULL){
		daslog_error_v("Source %s has %zu base URL(s), there is no number %zu",
		               pThis->sUrl, DasStreamSrc_urlCount(pThis), uBase);
		return 0;
	}

	/* Every reason is logged, not just the first: a blank requisition lists
	   the required keys up front, so the first bad entry is often "min is
	   required" while the user's typo sits further down */
	size_t uBad = 0;
	if(DasSrc_check(pThis, pReq, &uBad) != DAS_OKAY){
		for(size_t u = 0; u < pReq->uEntry; ++u)
			if(pReq->aEntry[u].verdict == das_req_bad) daslog_error_v("%s", pReq->aEntry[u].sWhy);
		return 0;
	}

	_out_t out = {NULL, 0, 0};
	char sWhy[1024];
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		const DasSrcCtrl* pC = p->pCtrl;
		if(pC == NULL) continue;

		const char* sVal = _reqValueFor(pReq, u);
		bool bDefault = false;
		if(sVal == NULL){
			if(!pC->bRequired || (p->sValue == NULL)) continue;
			sVal = p->sValue;
			bDefault = true;
		}
		/* A required default fills a hole, it never replaces what another
		   control already put in a shared parameter */
		if(bDefault && _outFind(&out, pC->sParam)) continue;

		/* A value equal to the default carries no information unless the
		   server insists on hearing it */
		if(!bDefault && !pC->bRequired && _sameAsDefault(p, sVal)) continue;

		if(!_translate(pC, sVal, &out, sWhy, sizeof(sWhy))){
			daslog_error_v("%s", sWhy);
			_outClear(&out);
			return 0;
		}
	}

	for(size_t u = 0; u < pReq->uRaw; ++u){
		if(!_outSet(&out, pReq->aRaw[u].sName, pReq->aRaw[u].sVal)){ _outClear(&out); return 0; }
	}

	/* Protocol parameters the node marks required, whatever supplies them */
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pS->pParams); pEl; pEl = pEl->next){
		if(!_jtrue(_jget(pEl->value, "required"))) continue;
		const char* sName = pEl->name->string;
		if(_outFind(&out, sName) != NULL) continue;

		size_t n = snprintf(sWhy, sizeof(sWhy), "Required parameter %s missing, set it with", sName);
		int nKeys = 0;
		for(size_t i = 0; i < pThis->uProp; ++i){
			const DasSrcCtrl* pC = pThis->aProp[i].pCtrl;
			if(pC && (strcmp(pC->sParam, sName) == 0)){
				n = _catf(sWhy, sizeof(sWhy), n, "%s %s=VALUE", nKeys ? " or" : "", pThis->aProp[i].sKey);
				++nKeys;
			}
		}
		if(nKeys == 0) _catf(sWhy, sizeof(sWhy), n, " a raw parameter, no interface key sets it");
		daslog_error_v("%s", sWhy);
		_outClear(&out);
		return 0;
	}

	size_t u = _catf(sBuf, uLen, 0, "%s", sBase);
	size_t uNeed = strlen(sBase);
	const char* sJoin = strchr(sBase, '?') ? "&" : "?";
	for(size_t i = 0; i < out.u; ++i){
		size_t uEnc = das_url_encode(out.a[i].sVal, NULL, 0) + 1;
		char* sEnc = (char*)malloc(uEnc);
		if(sEnc == NULL){ _outClear(&out); return 0; }
		das_url_encode(out.a[i].sVal, sEnc, uEnc);
		u = _catf(sBuf, uLen, u, "%s%s=%s", sJoin, out.a[i].sName, sEnc);
		uNeed += strlen(sJoin) + strlen(out.a[i].sName) + 1 + strlen(sEnc);
		free(sEnc);
		sJoin = "&";
	}
	_outClear(&out);
	return uNeed;
}

/* ************************************************************************* */
/* ************************************************************************* */
/* The sources under a container */

size_t dassrc_loadable(const DasNode* pNode, const char** aNames, size_t uMax)
{
	if((pNode == NULL) || !DasNode_isCatalog(pNode)) return 0;
	size_t uKids = DasNode_subCount(pNode);
	size_t uFound = 0;
	for(size_t u = 0; u < uKids; ++u){
		if(DasNode_subType(pNode, u) != d2node_stream_src) continue;
		if(uFound < uMax) aNames[uFound] = DasNode_subName(pNode, u);
		++uFound;
	}
	return uFound;
}
