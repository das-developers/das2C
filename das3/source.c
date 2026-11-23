/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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
 * and returned, never raised through daserrf(), because this code
 * meets bad catalogs and typos every day and the default error
 * disposition would exit the program over either.  Clients that catch
 * log messages see everything; clients that do not still get verdicts.
 *
 * Grew out of the pilot in utilities/das_fetch.c, which carried the
 * translation rules for two node layouts.  Those rules are not written
 * down anywhere else yet; until the catalog spec states them, the
 * comments on _DasSrcCtrl_translate() are the reference.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

#include "util.h"
#include "str.h"
#include "log.h"
#include "time.h"
#include "node.h"
#include "node_p.h"
#include "source_p.h"

/* Section names as the current catalogs spell them.  Keys are built from
 * these; input accepts the singular forms too, see _das_src_secByName(). */
static const char* g_sSecName[DASSRCSEC_SIZE] = {"coords", "data", "formats", "options"};

/* Scalars copied out of the document are short in practice, the longest
 * seen is a few dozen bytes; anything past this is cut rather than
 * carried. */
#define SCALAR_SZ 1024

/* ************************************************************************* */
/* Small string and JSON helpers.  DasJdo_subtree() takes slash paths, so the
 * dots in parameter names such as "read.time.min" are not special to it. */

static bool _dassrc_strieq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return false;
	while(*a && *b){
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		++a; ++b;
	}
	return (*a == '\0') && (*b == '\0');
}

static void _dassrc_strcpyz(char* sDest, const char* sSrc, size_t uDest)
{
	if(uDest == 0) return;
	strncpy(sDest, sSrc ? sSrc : "", uDest - 1);
	sDest[uDest - 1] = '\0';
}

static const DasJdo* _dassrc_jget(const DasJdo* p, const char* sPath)
{
	return ((p == NULL)||(sPath == NULL)) ? NULL : DasJdo_subtree(p, sPath);
}

static const char* _dassrc_jstr(const DasJdo* p, const char* sPath)
{
	const DasJdo* pSub = _dassrc_jget(p, sPath);
	return ((pSub != NULL)&&(pSub->type == DASJSONTYPE_STR)) ? DasJdo_str(pSub) : NULL;
}

static bool _dassrc_jisdict(const DasJdo* p){ return (p != NULL)&&(p->type == DASJSONTYPE_DICT); }
static bool _dassrc_jisary(const DasJdo* p){  return (p != NULL)&&(p->type == DASJSONTYPE_ARY); }
static bool _dassrc_jtrue(const DasJdo* p){   return (p != NULL)&&(p->type == DASJSONTYPE_TRUE); }
static bool _dassrc_jisbool(const DasJdo* p){
	return (p != NULL)&&((p->type == DASJSONTYPE_TRUE)||(p->type == DASJSONTYPE_FALSE));
}
static size_t _dassrc_jlen(const DasJdo* p)
{
	if(_dassrc_jisdict(p)) return ((const DasJsonDict*)p->value)->length;
	if(_dassrc_jisary(p))  return ((const DasJsonAry*)p->value)->length;
	return 0;
}

/* dasstr_new() returns NULL for "", which here would turn an empty default
   into no default.  Empty strings are copied as empty strings. */
static char* _dassrc_strdupz(const char* s)
{
	if((s == NULL)||(s[0] == '\0')) return (char*)calloc(1, 1);
	return dasstr_new(s);
}

/* Every string a source keeps, keys, default values, enum items and range
 * ends, is a separate small allocation, freed with the source.  A possible
 * optimization, not taken: the source owns pDoc for its whole life, so
 * plain JSON strings could be referenced in place and only numbers, which
 * need formatting, copied.  That would remove most of these allocations
 * at the cost of a copied-or-borrowed flag per string.  A full string
 * arena was considered and rejected: it needs an exact size pass that
 * duplicates the formatting logic below, and it hides overruns from
 * valgrind.  Revisit if a heap profile of a browser build points here. */
static char* _dassrc_scalarDup(const DasJdo* p)
{
	char sBuf[SCALAR_SZ];
	if(!_das_json_scalar(p, sBuf, sizeof(sBuf))) return NULL;
	return _dassrc_strdupz(sBuf);
}

/* The value of a list property, a JSON array, as its items joined with
   commas; NULL for an empty array, there is no default selection */
static char* _dassrc_listDup(const DasJdo* p)
{
	char sBuf[SCALAR_SZ];
	if(!_das_json_value(p, sBuf, sizeof(sBuf))) return NULL;
	return _dassrc_strdupz(sBuf);
}

/* Split a list value at its commas.  Items are trimmed of blanks and may
 * come out empty, the caller decides what that means.  Returns one
 * allocation, the item pointers followed by the text they point into, to
 * be freed with free(); NULL when out of memory.  "" has no items. */
static char** _dassrc_newSplit(const char* sVal, size_t* puItems)
{
	*puItems = 0;
	size_t uMax = 1;
	for(const char* p = sVal; *p; ++p) if(*p == ',') ++uMax;
	size_t uText = strlen(sVal) + 1;
	char** aItem = (char**)malloc(uMax * sizeof(char*) + uText);
	if(aItem == NULL) return NULL;
	char* sText = (char*)(aItem + uMax);
	memcpy(sText, sVal, uText);

	char* pBeg = sText;
	while(*pBeg == ' ') ++pBeg;
	if(*pBeg == '\0') return aItem;
	while(pBeg != NULL){
		char* pComma = strchr(pBeg, ',');
		if(pComma) *pComma = '\0';
		while(*pBeg == ' ') ++pBeg;
		char* pEnd = pBeg + strlen(pBeg);
		while((pEnd > pBeg) && (pEnd[-1] == ' ')) --pEnd;
		*pEnd = '\0';
		aItem[*puItems] = pBeg;
		*puItems += 1;
		pBeg = pComma ? pComma + 1 : NULL;
	}
	return aItem;
}

/* True for a value that says nothing: "", and for a list also blanks
   alone, which split into no items */
static bool _dassrc_noValue(const char* sVal, bool bList)
{
	if(bList) while(*sVal == ' ') ++sVal;
	return *sVal == '\0';
}

/* True if two list values hold the same items, in any order and letter
   case.  Lists never repeat an item, so equal counts and one way
   containment is enough. */
static bool _dassrc_sameItems(const char* sA, const char* sB)
{
	size_t uA = 0, uB = 0;
	char** aA = _dassrc_newSplit(sA, &uA);
	char** aB = _dassrc_newSplit(sB, &uB);
	bool bSame = (aA != NULL) && (aB != NULL) && (uA == uB);
	for(size_t i = 0; bSame && (i < uA); ++i){
		bool bHit = false;
		for(size_t j = 0; !bHit && (j < uB); ++j) bHit = _dassrc_strieq(aA[i], aB[j]);
		bSame = bHit;
	}
	free(aA);
	free(aB);
	return bSame;
}

static bool _dassrc_parseBool(const char* s, bool* pOut)
{
	if(s == NULL) return false;
	if(_dassrc_strieq(s,"true")||_dassrc_strieq(s,"yes")||_dassrc_strieq(s,"on")||_dassrc_strieq(s,"1")||
	   _dassrc_strieq(s,"t")||_dassrc_strieq(s,"y")){ *pOut = true; return true; }
	if(_dassrc_strieq(s,"false")||_dassrc_strieq(s,"no")||_dassrc_strieq(s,"off")||_dassrc_strieq(s,"0")||
	   _dassrc_strieq(s,"f")||_dassrc_strieq(s,"n")){ *pOut = false; return true; }
	return false;
}

/* snprintf that appends and never runs the cursor past the buffer */
static size_t _dassrc_catf(char* sBuf, size_t uLen, size_t u, const char* sFmt, ...)
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

const char* dassrcsec_name(das_srcsec sec)
{
	return ((sec >= 0)&&(sec < DASSRCSEC_SIZE)) ? g_sSecName[sec] : "";
}

bool _das_src_secByName(const char* s, das_srcsec* pSec)
{
	if(_dassrc_strieq(s,"coord")||_dassrc_strieq(s,"coords")||_dassrc_strieq(s,"coordinate")||_dassrc_strieq(s,"coordinates")){
		*pSec = DASSRCSEC_COORD; return true;
	}
	if(_dassrc_strieq(s,"data")){ *pSec = DASSRCSEC_DATA; return true; }
	if(_dassrc_strieq(s,"format")||_dassrc_strieq(s,"formats")||_dassrc_strieq(s,"fmt")){
		*pSec = DASSRCSEC_FORMAT; return true;
	}
	if(_dassrc_strieq(s,"option")||_dassrc_strieq(s,"options")||_dassrc_strieq(s,"opt")||_dassrc_strieq(s,"opts")){
		*pSec = DASSRCSEC_OPTION; return true;
	}
	return false;
}

const char* _das_src_propName(const char* s)
{
	if(s == NULL) return "";
	if(_dassrc_strieq(s, "minimum")||_dassrc_strieq(s, "min"))        return "min";
	if(_dassrc_strieq(s, "maximum")||_dassrc_strieq(s, "max"))        return "max";
	if(_dassrc_strieq(s, "resolution")||_dassrc_strieq(s, "res"))     return "res";
	if(_dassrc_strieq(s, "interval")||_dassrc_strieq(s, "inter"))     return "inter";
	if(_dassrc_strieq(s, "enable")||_dassrc_strieq(s, "enabled"))     return "enabled";
	return s;
}

bool _das_src_propEq(const char* a, const char* b)
{
	return _dassrc_strieq(_das_src_propName(a), _das_src_propName(b));
}

int _das_src_splitKey(const char* sKey, char aParts[3][DASID_SZ])
{
	if(sKey == NULL) return 0;
	int n = 0;
	const char* pBeg = sKey;
	while(true){
		const char* pEnd = strchr(pBeg, '.');
		size_t uLen = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		if((uLen == 0)||(uLen >= DASID_SZ)||(n == 3)) return 0;
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
static bool _dassrc_typeByName(const char* s, das_srcproptype* pType)
{
	if(s == NULL) return false;
	if(_dassrc_strieq(s,"boolean")||_dassrc_strieq(s,"bool")){ *pType = DASSRCPROPTYPE_BOOL; return true; }
	if(_dassrc_strieq(s,"integer")||_dassrc_strieq(s,"int")){ *pType = DASSRCPROPTYPE_INT; return true; }
	if(_dassrc_strieq(s,"real")||_dassrc_strieq(s,"double")||_dassrc_strieq(s,"float")){
		*pType = DASSRCPROPTYPE_REAL; return true;
	}
	if(_dassrc_strieq(s,"isotime")||_dassrc_strieq(s,"time")||_dassrc_strieq(s,"datetime")){
		*pType = DASSRCPROPTYPE_TIME; return true;
	}
	if(_dassrc_strieq(s,"enum")){ *pType = DASSRCPROPTYPE_ENUM; return true; }
	if(_dassrc_strieq(s,"string")||_dassrc_strieq(s,"text")){ *pType = DASSRCPROPTYPE_TEXT; return true; }
	return false;
}

/* Preference order: a set enum list, the property's type, a boolean
 * default, a flag toggle, the protocol parameter's type, the shape of the
 * value, then text.  The enum list goes first because a property declared
 * "string" with a list of choices is still a list of choices to anyone
 * presenting it.  A flag set parameter type is a server side packing
 * detail and says nothing about the single toggle that feeds it. */
static void _DasSrcProp_resolveType(DasSrcProp* p)
{
	p->sTypeName = _dassrc_jstr(p->pJdo, "type");
	if(p->sTypeName == NULL) p->sTypeName = "";
	const DasSrcCtrl* pC = p->pCtrl;
	const DasJdo* pVal = _dassrc_jget(p->pJdo, "value");

	if(pC && (pC->uEnum > 0)){ p->type = DASSRCPROPTYPE_ENUM; return; }
	if(_dassrc_typeByName(p->sTypeName, &(p->type))){
		if(p->type != DASSRCPROPTYPE_ENUM) return;
	}
	if(_dassrc_jisbool(pVal) || (pC && !p->bMulti && (_dassrc_jstr(pC->pSet, "flag") != NULL))){
		p->type = DASSRCPROPTYPE_BOOL; return;
	}
	const char* sParamType = pC ? _dassrc_jstr(pC->pParam, "type") : NULL;
	if(_dassrc_typeByName(sParamType, &(p->type)) && (p->type != DASSRCPROPTYPE_ENUM)){
		if(p->sTypeName[0] == '\0') p->sTypeName = sParamType;
		return;
	}
	if((pVal != NULL)&&(pVal->type == DASJSONTYPE_NUM)){ p->type = DASSRCPROPTYPE_REAL; return; }
	p->type = DASSRCPROPTYPE_TEXT;
}

/* "multi": true, or [lo, hi] item count bounds.  Anything else, false
 * included, leaves an ordinary one value property. */
static void _DasSrcProp_fillMulti(DasSrcProp* p)
{
	p->uMultiLo = 1;
	p->uMultiHi = 1;
	const DasJdo* pM = _dassrc_jget(p->pJdo, "multi");
	if(pM == NULL) return;
	if(_dassrc_jtrue(pM)){
		p->bMulti = true; p->uMultiLo = 0; p->uMultiHi = SIZE_MAX;
		return;
	}
	if(!_dassrc_jisary(pM)) return;

	char sLo[32], sHi[32];
	const DasJsonAryEl* pEl = DasJdo_aryFirst(pM);
	char* pEnd = NULL;
	if((pEl == NULL)||(pEl->next == NULL)||(pEl->next->next != NULL)||
	   !_das_json_scalar(pEl->value, sLo, sizeof(sLo))||
	   !_das_json_scalar(pEl->next->value, sHi, sizeof(sHi))){
		daslog_warnf("Property %s has a multi member that is neither true nor "
		              "[least, most], treating it as true", p->sKey);
		p->bMulti = true; p->uMultiLo = 0; p->uMultiHi = SIZE_MAX;
		return;
	}
	long nLo = strtol(sLo, &pEnd, 10);
	bool bOk = (pEnd != sLo) && (*pEnd == '\0') && (nLo >= 0);
	long nHi = strtol(sHi, &pEnd, 10);
	bOk = bOk && (pEnd != sHi) && (*pEnd == '\0') && (nHi >= nLo) && (nHi > 0);
	p->bMulti = true;
	p->uMultiLo = bOk ? (size_t)nLo : 0;
	p->uMultiHi = bOk ? (size_t)nHi : SIZE_MAX;
	if(!bOk)
		daslog_warnf("Property %s has unusable multi bounds [%s, %s], not "
		              "bounding it", p->sKey, sLo, sHi);
}

/* Enum entries are usually dicts carrying value plus per item pval or flag
 * overrides, but a bare scalar list is accepted too. */
static bool _DasSrcCtrl_fillEnum(DasSrcCtrl* p)
{
	const DasJdo* pEnum = _dassrc_jget(p->pSet, "enum");
	if(!_dassrc_jisary(pEnum)) return true;

	size_t uMax = _dassrc_jlen(pEnum);
	if(uMax == 0) return true;
	p->aEnum = (char**)calloc(uMax, sizeof(char*));
	if(p->aEnum == NULL) return false;

	for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
		const DasJdo* pVal = _dassrc_jisdict(pEl->value) ? _dassrc_jget(pEl->value, "value") : pEl->value;
		char* s = _dassrc_scalarDup(pVal);
		if(s == NULL) continue;
		p->aEnum[p->uEnum] = s;
		p->uEnum += 1;
	}
	return true;
}

static void _DasSrcCtrl_fillRange(DasSrcCtrl* p)
{
	const DasSrcProp* pProp = p->pProp;
	const DasJdo* pRange = _dassrc_jget(p->pSet, "range");
	if(!_dassrc_jisary(pRange)) pRange = _dassrc_jget(p->pParam, "range");
	if(!_dassrc_jisary(pRange) && (pProp->sec == DASSRCSEC_COORD) &&
	   (_das_src_propEq(pProp->sName, "min") || _das_src_propEq(pProp->sName, "max")))
		pRange = _dassrc_jget(pProp->pGroup->pJdo, "validRange");
	if(!_dassrc_jisary(pRange)) return;

	const DasJsonAryEl* pEl = DasJdo_aryFirst(pRange);
	if(pEl == NULL) return;
	p->sLo = _dassrc_scalarDup(pEl->value);
	if(pEl->next) p->sHi = _dassrc_scalarDup(pEl->next->value);
}

/* Release what a control owns; the control itself lives in aCtrl */
static void _DasSrcCtrl_clear(DasSrcCtrl* p)
{
	if(p == NULL) return;
	for(size_t v = 0; v < p->uEnum; ++v) free(p->aEnum[v]);
	free(p->aEnum);
	free(p->sLo);
	free(p->sHi);
}

/* A member of a group dict is a property if it is a dict that looks like
 * one.  In the current layout every member of "props" qualifies; in the
 * 2018 layout properties sit beside the group's name and title
 * strings, and a units dict with a value counts as the units property
 * just as it does under props today. */
static bool _dassrc_isPropDict(const DasJdo* p)
{
	if(!_dassrc_jisdict(p)) return false;
	return (_dassrc_jget(p, "value") != NULL)||(_dassrc_jget(p, "set") != NULL)||(_dassrc_jget(p, "type") != NULL);
}

static const DasJdo* _dassrc_groupProps(const DasJdo* pGroup)
{
	const DasJdo* pProps = _dassrc_jget(pGroup, "props");
	return _dassrc_jisdict(pProps) ? pProps : pGroup;   /* 2018 layout: no props */
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
static const DasJdo* _dassrc_setBlock(const DasJdo* pJdo)
{
	const DasJdo* pSet = _dassrc_jget(pJdo, "set");
	return (_dassrc_jisdict(pSet) && (_dassrc_jstr(pSet, "param") != NULL)) ? pSet : NULL;
}

static void _dassrc_walkProp(_walk_t* pW, DasSrcGroup* pG, const char* sName, const DasJdo* pJdo)
{
	if(pW->pSrc == NULL){
		pW->uProp += 1;
		if(_dassrc_setBlock(pJdo) != NULL) pW->uCtrl += 1;
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

	_DasSrcProp_fillMulti(p);
	p->sValue = p->bMulti ? _dassrc_listDup(_dassrc_jget(pJdo, "value")) :
	                        _dassrc_scalarDup(_dassrc_jget(pJdo, "value"));

	const DasJdo* pSet = _dassrc_jget(pJdo, "set");
	if(_dassrc_jisdict(pSet)){
		const char* sParam = _dassrc_jstr(pSet, "param");
		if(sParam == NULL){
			daslog_warnf("Property %s has a set block that names no parameter, "
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
			pC->pParam = _dassrc_jget(pW->pParams, sParam);
			pC->bRequired = _dassrc_jtrue(_dassrc_jget(pSet, "required")) || _dassrc_jtrue(_dassrc_jget(pC->pParam, "required"));
			p->pCtrl = pC;
			if(!_DasSrcCtrl_fillEnum(pC)){ pW->bFail = true; return; }
			_DasSrcCtrl_fillRange(pC);
		}
	}
	_DasSrcProp_resolveType(p);
	if(p->bMulti && (p->type == DASSRCPROPTYPE_BOOL)){
		/* on or off has nothing to list */
		daslog_warnf("Property %s is a boolean, ignoring its multi member", p->sKey);
		p->bMulti = false;
		p->uMultiLo = 1;
		p->uMultiHi = 1;
	}

	if(_das_src_propEq(sName, "enabled") && (pG->nEnable < 0)){
		pG->nEnable = (int)(pW->uProp - pG->uFirst);
		pG->sXor = _dassrc_jstr(pJdo, "xorGroup");
	}
	pW->uProp += 1;
	pG->uCount += 1;
}

static void _dassrc_walkGroup(_walk_t* pW, das_srcsec sec, const char* sName, const DasJdo* pJdo)
{
	if(!_dassrc_jisdict(pJdo)) return;
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

		const DasJdo* pMime = _dassrc_jget(pJdo, "mimeTypes");
		if(_dassrc_jisary(pMime) && (_dassrc_jlen(pMime) > 0)){
			pG->aMime = (const char**)calloc(_dassrc_jlen(pMime), sizeof(char*));
			if(pG->aMime == NULL){ pW->bFail = true; return; }
			for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pMime); pEl; pEl = pEl->next){
				if(DasJdo_str(pEl->value) == NULL) continue;
				pG->aMime[pG->uMime] = DasJdo_str(pEl->value);
				pG->uMime += 1;
			}
		}
		pW->pSrc->aSecCount[sec] += 1;
	}
	pW->uGroup += 1;

	const DasJdo* pProps = _dassrc_groupProps(pJdo);
	for(const DasJsonDictEl* pEl = DasJdo_dictFirst(pProps); pEl; pEl = pEl->next){
		if(!_dassrc_isPropDict(pEl->value)) continue;
		_dassrc_walkProp(pW, pG, pEl->name->string, pEl->value);
	}
}

/* Groups of a section in presentation order: "time" first for coordinates,
 * otherwise as the node lists them.  Forms want time on top and nodes do
 * not always put it there. */
static void _dassrc_walkSection(_walk_t* pW, das_srcsec sec, const DasJdo* pSec)
{
	if(!_dassrc_jisdict(pSec)) return;
	if(pW->pSrc != NULL) pW->pSrc->aSecFirst[sec] = pW->uGroup;
	if(sec == DASSRCSEC_COORD){
		for(const DasJsonDictEl* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next)
			if(_dassrc_strieq(pEl->name->string, "time")) _dassrc_walkGroup(pW, sec, pEl->name->string, pEl->value);
	}
	for(const DasJsonDictEl* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next){
		if((sec == DASSRCSEC_COORD) && _dassrc_strieq(pEl->name->string, "time")) continue;
		_dassrc_walkGroup(pW, sec, pEl->name->string, pEl->value);
	}
}

static void _dassrc_walkIface(_walk_t* pW, const DasJdo* pIface)
{
	const DasJdo* pCoords = _dassrc_jget(pIface, "coords");
	if(pCoords == NULL) pCoords = _dassrc_jget(pIface, "coordinates");   /* 2018 layout */
	_dassrc_walkSection(pW, DASSRCSEC_COORD, pCoords);
	_dassrc_walkSection(pW, DASSRCSEC_DATA, _dassrc_jget(pIface, "data"));
	_dassrc_walkSection(pW, DASSRCSEC_FORMAT, _dassrc_jget(pIface, "formats"));

	/* Options are one group with no enclosing section */
	const DasJdo* pOpts = _dassrc_jget(pIface, "options");
	if(_dassrc_jisdict(pOpts)){
		if(pW->pSrc != NULL) pW->pSrc->aSecFirst[DASSRCSEC_OPTION] = pW->uGroup;
		_dassrc_walkGroup(pW, DASSRCSEC_OPTION, "", pOpts);
	}
}

/* ************************************************************************* */
/* Examples, parsed into requisitions when the source is built */

/* "coords/time/props/min" -> "coords.time.min".  The section token is
 * folded to the current spelling so a saved requisition reads the same
 * whatever node it came from. */
static bool _dassrc_exPathToKey(const char* sPath, char* sKey, size_t uLen)
{
	size_t u = 0;
	int nTok = 0;
	const char* pBeg = sPath;
	sKey[0] = '\0';
	while(*pBeg){
		const char* pEnd = strchr(pBeg, '/');
		size_t n = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		char sTok[DASID_SZ];
		if((n == 0)||(n >= sizeof(sTok))) return false;
		memcpy(sTok, pBeg, n); sTok[n] = '\0';
		const char* sOut = sTok;
		das_srcsec sec;
		if(strcmp(sTok, "props") == 0) sOut = NULL;
		else if((nTok == 0) && _das_src_secByName(sTok, &sec)) sOut = g_sSecName[sec];
		if(sOut != NULL){
			if(u + strlen(sOut) + 2 >= uLen) return false;
			u = _dassrc_catf(sKey, uLen, u, "%s%s", nTok ? "." : "", sOut);
			++nTok;
		}
		if(pEnd == NULL) break;
		pBeg = pEnd + 1;
	}
	return nTok > 0;
}

/* The flag value a control sends, without any prefix: the parameter's
 * flags entry for it, or the flag name itself */
static const char* _DasSrcCtrl_flagVal(const DasSrcCtrl* pC, const char* sFlag, char* sBuf, size_t uLen)
{
	char sPath[DASID_SZ + 8];
	snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
	const DasJdo* pF = _dassrc_jget(pC->pParam, sPath);
	if(!_das_json_scalar(_dassrc_jget(pF, "value"), sBuf, uLen)) _dassrc_strcpyz(sBuf, sFlag, uLen);
	return sBuf;
}

/* A flag named by a control's set block works one of two ways.  On a
 * boolean it is a switch: when on, the flag sends its own value, see
 * _DasSrcCtrl_flagVal().  On anything else it is a slot: the control's
 * value follows the flag's prefix, so an anode chooser set to 8 sends
 * "anode=8".  A flag named by an enum item instead is always a switch,
 * the item picks which flag goes out. */
static bool _DasSrcCtrl_flagIsSlot(const DasSrcCtrl* pC)
{
	return (_dassrc_jstr(pC->pSet, "flag") != NULL) &&
	       (pC->pProp->type != DASSRCPROPTYPE_BOOL);
}

/* ---- 2018 layout, begin: protocol level examples, read back into
   controls.  Delete down to the end marker. ---- */

/* The prefix of a control's set block flag, "" when none */
static const char* _DasSrcCtrl_flagPrefix(const DasSrcCtrl* pC)
{
	const char* sFlag = _dassrc_jstr(pC->pSet, "flag");
	if(sFlag == NULL) return "";
	char sPath[DASID_SZ + 8];
	snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
	const char* sPre = _dassrc_jstr(_dassrc_jget(pC->pParam, sPath), "prefix");
	return sPre ? sPre : "";
}

/* The interface value whose protocol value is sPVal: an enum item with
 * that pval, else an item spelled sPVal, else sPVal as is.  The inverse of
 * the enum step in _DasSrcCtrl_translate(). */
static const char* _DasSrcCtrl_fromPVal(const DasSrcCtrl* pC, const char* sPVal, char* sBuf, size_t uLen)
{
	_dassrc_strcpyz(sBuf, sPVal, uLen);
	const DasJdo* pEnum = _dassrc_jget(pC->pSet, "enum");
	if(!_dassrc_jisary(pEnum)) return sBuf;
	char sItem[SCALAR_SZ], sP[SCALAR_SZ];
	for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
		const DasJdo* pItem = _dassrc_jisdict(pEl->value) ? _dassrc_jget(pEl->value, "value") : pEl->value;
		if(!_das_json_scalar(pItem, sItem, sizeof(sItem))) continue;
		bool bHit = _das_json_scalar(_dassrc_jget(pEl->value, "pval"), sP, sizeof(sP)) ?
		            _dassrc_strieq(sP, sPVal) : _dassrc_strieq(sItem, sPVal);
		if(bHit){ _dassrc_strcpyz(sBuf, sItem, uLen); break; }
	}
	return sBuf;
}

/* A 2018 example is raw protocol parameters.  Each is mapped back onto
 * the control that would have produced it, so the requisition looks the
 * same as one from a current node.  Parameters no control explains are
 * kept as raw parameters, nothing is dropped. */
static bool _DasSrc_exampleFromParams(DasSrc* pThis, DasReq* pReq, const DasJdo* pParams)
{
	char sVal[SCALAR_SZ], sFlagVal[SCALAR_SZ], sPVal[SCALAR_SZ];
	for(const DasJsonDictEl* pP = DasJdo_dictFirst(pParams); pP; pP = pP->next){
		const char* sName = pP->name->string;
		if(!_das_json_scalar(pP->value, sVal, sizeof(sVal))) continue;

		/* the controls feeding this parameter */
		bool bFlagSet = false, bHandled = false;
		for(size_t u = 0; u < pThis->uProp; ++u){
			const DasSrcCtrl* pC = pThis->aProp[u].pCtrl;
			if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;
			if(_dassrc_jstr(pC->pSet, "flag") != NULL){ bFlagSet = true; break; }
		}

		if(bFlagSet){
			const char* sSep = NULL;
			for(size_t u = 0; (sSep == NULL) && (u < pThis->uProp); ++u){
				const DasSrcCtrl* pC = pThis->aProp[u].pCtrl;
				if((pC == NULL)||(strcmp(pC->sParam, sName) != 0)) continue;
				sSep = _dassrc_jstr(pC->pParam, "flagSep");
				if(sSep == NULL) sSep = _dassrc_jstr(pC->pParam, "flag_sep");
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
					const char* sFlag = _dassrc_jstr(pC->pSet, "flag");
					if(sFlag == NULL) continue;

					/* A slot is recognized by its prefix alone; without one,
					   a token could belong to any slot and is left raw */
					if(_DasSrcCtrl_flagIsSlot(pC)){
						const char* sPre = _DasSrcCtrl_flagPrefix(pC);
						size_t uPre = strlen(sPre);
						if((uPre == 0)||(strlen(pTok) <= uPre)) continue;
						char sHead[SCALAR_SZ];
						_dassrc_strcpyz(sHead, pTok, (uPre + 1 < sizeof(sHead)) ? uPre + 1 : sizeof(sHead));
						if(!_dassrc_strieq(sHead, sPre)) continue;
						_DasSrcCtrl_fromPVal(pC, pTok + uPre, sFlagVal, sizeof(sFlagVal));
						if(DasReq_set(pReq, pThis->aProp[u].sKey, sFlagVal) != DAS_OKAY) return false;
						bFound = true;
						continue;
					}

					_DasSrcCtrl_flagVal(pC, sFlag, sFlagVal, sizeof(sFlagVal));
					if(_dassrc_strieq(sFlagVal, pTok)){
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

			const DasJdo* pEnum = _dassrc_jget(pC->pSet, "enum");
			if(_dassrc_jisary(pEnum)){
				/* which choice sends this protocol value? */
				for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pEnum); !bHandled && pEl; pEl = pEl->next){
					const DasJdo* pItem = _dassrc_jisdict(pEl->value) ? _dassrc_jget(pEl->value, "value") : pEl->value;
					char sItem[SCALAR_SZ];
					if(!_das_json_scalar(pItem, sItem, sizeof(sItem))) continue;
					bool bHit = _das_json_scalar(_dassrc_jget(pEl->value, "pval"), sPVal, sizeof(sPVal)) ?
					            _dassrc_strieq(sPVal, sVal) : _dassrc_strieq(sItem, sVal);
					if(bHit){
						if(DasReq_set(pReq, p->sKey, sItem) != DAS_OKAY) return false;
						bHandled = true;
					}
				}
				continue;
			}
			if(_das_json_scalar(_dassrc_jget(pC->pSet, "pval"), sPVal, sizeof(sPVal))){
				/* a fixed protocol value stands for one state of a toggle */
				if(_dassrc_strieq(sPVal, sVal)){
					bool bOn = true;
					const DasJdo* pTrig = _dassrc_jget(pC->pSet, "value");
					if(_dassrc_jisbool(pTrig)) bOn = _dassrc_jtrue(pTrig);
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
/* ---- 2018 layout, end ---- */

static bool _DasSrc_loadExamples(DasSrc* pThis)
{
	const DasJdo* pEx = _dassrc_jget(pThis->pIface, "examples");
	const DasJdo* pProto = _dassrc_jget(pThis->pDoc, "protocol");
	bool bLegacyEx = false;   /* 2018 layout, through the if below */
	if(!_dassrc_jisary(pEx)){
		pEx = _dassrc_jget(pProto, "examples");
		bLegacyEx = _dassrc_jisdict(pEx);
		if(!bLegacyEx) return true;
	}
	size_t uMax = _dassrc_jlen(pEx);
	if(uMax == 0) return true;
	pThis->aExample = (DasReq**)calloc(uMax, sizeof(DasReq*));
	if(pThis->aExample == NULL) return false;

	char sKey[256], sVal[SCALAR_SZ];
	/* ---- 2018 layout, begin ---- */
	if(bLegacyEx){
		for(const DasJsonDictEl* pEl = DasJdo_dictFirst(pEx); pEl; pEl = pEl->next){
			DasReq* pReq = new_DasReq();
			if(pReq == NULL) return false;
			pThis->aExample[pThis->uExample] = pReq;
			pThis->uExample += 1;
			const char* sLabel = _dassrc_jstr(pEl->value, "title");
			if(sLabel == NULL) sLabel = _dassrc_jstr(pEl->value, "name");
			if(sLabel == NULL) sLabel = pEl->name->string;
			DasReq_setLabel(pReq, sLabel);
			DasSrc_stampReq(pThis, pReq);
			const DasJdo* pParams = _dassrc_jget(pEl->value, "http_params");
			if(_dassrc_jisdict(pParams) && !_DasSrc_exampleFromParams(pThis, pReq, pParams)) return false;
		}
		return true;
	}
	/* ---- 2018 layout, end ---- */

	for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pEx); pEl; pEl = pEl->next){
		DasReq* pReq = new_DasReq();
		if(pReq == NULL) return false;
		pThis->aExample[pThis->uExample] = pReq;
		pThis->uExample += 1;
		const char* sLabel = _dassrc_jstr(pEl->value, "label");
		if(sLabel == NULL) sLabel = _dassrc_jstr(pEl->value, "title");
		if(sLabel == NULL) sLabel = _dassrc_jstr(pEl->value, "name");
		DasReq_setLabel(pReq, sLabel);
		DasSrc_stampReq(pThis, pReq);
		const DasJdo* pSettings = _dassrc_jget(pEl->value, "settings");
		for(const DasJsonDictEl* pP = DasJdo_dictFirst(pSettings); pP; pP = pP->next){
			if(!_dassrc_exPathToKey(pP->name->string, sKey, sizeof(sKey))){
				daslog_warnf("Ignoring example setting '%s' in %s, the path makes no sense",
				              pP->name->string, pThis->sUrl);
				continue;
			}
			if(!_das_json_value(pP->value, sVal, sizeof(sVal))) continue;
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
		_DasSrcCtrl_clear(pThis->aProp[u].pCtrl);
	}
	free(pThis->aProp);
	free(pThis->aCtrl);
	for(size_t u = 0; u < pThis->uGroup; ++u) free((void*)pThis->aGroup[u].aMime);  /* quiet msvc */
	free(pThis->aGroup);
	free(pThis->pDoc);
	free(pThis->sPath);
	free(pThis->sUrl);
	free(pThis);
}

DasSrc* new_DasSrc_json(const char* sDoc, size_t uLen, const char* sPathUri, const char* sUrl)
{
	const char* sWhere = sUrl ? sUrl : (sPathUri ? sPathUri : "(memory)");
	DasJdo* pDoc = new_DasJdo_parse(sDoc, uLen);
	if(!_dassrc_jisdict(pDoc)){
		daslog_errorf("Source document %s is not a JSON object", sWhere);
		free(pDoc);
		return NULL;
	}
	const char* sType = _dassrc_jstr(pDoc, "type");
	if(!_dassrc_strieq(sType, "HttpStreamSrc")){
		daslog_errorf("Node %s is of type %s, only HttpStreamSrc nodes describe data sources "
		               "this library can query", sWhere, sType ? sType : "(none)");
		free(pDoc);
		return NULL;
	}

	DasStreamSrc* pStream = (DasStreamSrc*)calloc(1, sizeof(DasStreamSrc));
	if(pStream == NULL){ free(pDoc); return NULL; }
	DasSrc* pThis = &(pStream->base);
	pThis->kind = DASSRCKIND_STREAM;
	pThis->pDoc = pDoc;

	/* A source given no URI has none, and a requisition it makes carries
	 * no stamp, see DasReq_setSrc(): nothing can hold it to one source */
	pThis->sPath = _dassrc_strdupz(sPathUri);
	pThis->sUrl = _dassrc_strdupz(sUrl);
	if((pThis->sPath == NULL)||(pThis->sUrl == NULL)){ del_DasSrc(pThis); return NULL; }

	pThis->pIface = _dassrc_jget(pDoc, "interface");
	if(!_dassrc_jisdict(pThis->pIface)) pThis->pIface = NULL;

	pStream->pProto = _dassrc_jget(pDoc, "protocol");
	if(!_dassrc_jisdict(pStream->pProto)) pStream->pProto = NULL;
	pStream->pParams = _dassrc_jget(pStream->pProto, "httpParams");
	if(pStream->pParams == NULL) pStream->pParams = _dassrc_jget(pStream->pProto, "http_params");   /* 2018 layout */
	pStream->pBaseUrls = _dassrc_jget(pStream->pProto, "baseUrls");
	if(pStream->pBaseUrls == NULL) pStream->pBaseUrls = _dassrc_jget(pStream->pProto, "base_urls");   /* 2018 layout */
	if(!_dassrc_jisary(pStream->pBaseUrls)) pStream->pBaseUrls = NULL;
	/* 2018 layout, both lines; nothing reads bLegacy */
	pThis->bLegacy = (_dassrc_jget(pStream->pProto, "http_params") != NULL) ||
	                 (_dassrc_jget(pThis->pIface, "coordinates") != NULL);

	/* Two passes over the same walk: count, allocate exactly that many,
	 * fill.  Groups, properties and controls are three blocks, so a source
	 * is a handful of allocations however large its interface, which
	 * matters on a heap that never shrinks, as in a browser build. */
	_walk_t w = {0, 0, 0, NULL, pStream->pParams, false};
	_dassrc_walkIface(&w, pThis->pIface);
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
	_dassrc_walkIface(&w, pThis->pIface);
	pThis->uGroup = w.uGroup;
	pThis->uProp = w.uProp;
	pThis->uCtrl = w.uCtrl;
	if(w.bFail || (uGroups != w.uGroup) || (uProps != w.uProp) || (uCtrls != w.uCtrl)){
		daslog_errorf("Couldn't build the interface of source %s", sWhere);
		del_DasSrc(pThis);
		return NULL;
	}

	if(!_DasSrc_loadExamples(pThis)){
		daslog_errorf("Couldn't read the examples of source %s", sWhere);
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
		size_t uKids = DasSrc_loadable(pNode, aNames, 8);
		char sMsg[512];
		size_t n = snprintf(sMsg, sizeof(sMsg), "Node %s is not a data source", DasNode_srcUrl(pNode));
		if(uKids > 0) n = _dassrc_catf(sMsg, sizeof(sMsg), n, ", its loadable children are:");
		for(size_t u = 0; (u < uKids) && (u < 8); ++u) n = _dassrc_catf(sMsg, sizeof(sMsg), n, " %s", aNames[u]);
		daslog_errorf("%s", sMsg);
		return NULL;
	}
	const DasJdo* pDom = DasNode_jdo(pNode, NULL);
	if(pDom == NULL) return NULL;

	/* A serialized copy keeps the source independent of the tree.  Nodes
	   are a few KB, so this is cheaper than sharing with a reference count
	   would be to get right. */
	size_t uLen = 0;
	char* sText = (char*)DasJdo_newMiniStr(pDom, &uLen);
	if(sText == NULL){
		daslog_errorf("Couldn't copy the document of node %s", DasNode_srcUrl(pNode));
		return NULL;
	}
	DasSrc* pThis = new_DasSrc_json(sText, uLen, DasNode_pathUri(pNode), DasNode_srcUrl(pNode));
	free(sText);
	return pThis;
}

/* ************************************************************************* */
/* Identity */

das_srckind DasSrc_kind(const DasSrc* pThis){ return pThis->kind; }
const char* DasSrc_path(const DasSrc* pThis){ return pThis->sPath; }
const char* DasSrc_url(const DasSrc* pThis){ return pThis->sUrl; }

static const char* _DasSrc_docStr(const DasSrc* pThis, const char* sPath)
{
	const char* s = _dassrc_jstr(pThis->pDoc, sPath);
	return s ? s : "";
}

const char* DasSrc_name(const DasSrc* pThis)
{
	const char* s = _dassrc_jstr(pThis->pDoc, "label");
	if(s == NULL) s = _dassrc_jstr(pThis->pDoc, "name");
	return s ? s : "";
}

const char* DasSrc_title(const DasSrc* pThis){ return _DasSrc_docStr(pThis, "title"); }
const char* DasSrc_version(const DasSrc* pThis){ return _DasSrc_docStr(pThis, "version"); }

/* The protocol section's login block; older nodes call it "authentication" */
static const DasJdo* _DasSrc_auth(const DasSrc* pThis)
{
	const DasJdo* pProto = _dassrc_jget(pThis->pDoc, "protocol");
	const DasJdo* pAuth = _dassrc_jget(pProto, "authorization");
	return pAuth ? pAuth : _dassrc_jget(pProto, "authentication");   /* 2018 layout */
}

bool DasSrc_needsAuth(const DasSrc* pThis)
{
	return _dassrc_jtrue(_dassrc_jget(_DasSrc_auth(pThis), "required"));
}

const char* DasSrc_authRealm(const DasSrc* pThis)
{
	const char* s = _dassrc_jstr(_DasSrc_auth(pThis), "realm");
	return s ? s : "";
}

static const DasJdo* _DasSrc_contacts(const DasSrc* pThis)
{
	const DasJdo* p = _dassrc_jget(pThis->pDoc, "contacts");
	if(!_dassrc_jisary(p)) p = _dassrc_jget(pThis->pDoc, "tech_contacts");
	return _dassrc_jisary(p) ? p : NULL;
}

size_t DasSrc_numContacts(const DasSrc* pThis){ return _dassrc_jlen(_DasSrc_contacts(pThis)); }

const char* DasSrc_contactAt(const DasSrc* pThis, size_t uIdx, const char** psEmail)
{
	const DasJsonAryEl* pEl = DasJdo_aryFirst(_DasSrc_contacts(pThis));
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	if(pEl == NULL) return NULL;
	const char* sName = _dassrc_jstr(pEl->value, "name");
	const char* sEmail = _dassrc_jstr(pEl->value, "email");
	if(psEmail) *psEmail = sEmail ? sEmail : "";
	return sName ? sName : "";
}

const DasJdo* DasSrc_jdo(const DasSrc* pThis, const char* sFragment)
{
	if((sFragment == NULL)||(sFragment[0] == '\0')) return pThis->pDoc;
	return DasJdo_subtree(pThis->pDoc, sFragment);
}

/* ************************************************************************* */
/* Groups */

size_t DasSrc_numGroups(const DasSrc* pThis, das_srcsec sec)
{
	return ((sec >= 0)&&(sec < DASSRCSEC_SIZE)) ? pThis->aSecCount[sec] : 0;
}

const DasSrcGroup* DasSrc_groupAt(const DasSrc* pThis, das_srcsec sec, size_t uIdx)
{
	if(uIdx >= DasSrc_numGroups(pThis, sec)) return NULL;
	return pThis->aGroup + pThis->aSecFirst[sec] + uIdx;
}

const DasSrcGroup* DasSrc_group(const DasSrc* pThis, das_srcsec sec, const char* sName)
{
	if(sName == NULL) sName = "";
	for(size_t u = 0; u < DasSrc_numGroups(pThis, sec); ++u){
		const DasSrcGroup* pG = DasSrc_groupAt(pThis, sec, u);
		if(_dassrc_strieq(pG->sName, sName)) return pG;
	}
	return NULL;
}

das_srcsec DasSrcGroup_section(const DasSrcGroup* pThis){ return pThis->sec; }
const char* DasSrcGroup_name(const DasSrcGroup* pThis){ return pThis->sName; }

const char* DasSrcGroup_label(const DasSrcGroup* pThis)
{
	const char* s = _dassrc_jstr(pThis->pJdo, "label");
	if(s == NULL) s = _dassrc_jstr(pThis->pJdo, "name");
	return s ? s : pThis->sName;
}

const char* DasSrcGroup_title(const DasSrcGroup* pThis)
{
	const char* s = _dassrc_jstr(pThis->pJdo, "title");
	return s ? s : "";
}

const DasSrcProp* DasSrcGroup_propAt(const DasSrcGroup* pThis, size_t uIdx)
{
	return (uIdx < pThis->uCount) ? pThis->pSrc->aProp + pThis->uFirst + uIdx : NULL;
}

const DasSrcProp* DasSrcGroup_enableProp(const DasSrcGroup* pThis)
{
	return (pThis->nEnable < 0) ? NULL : DasSrcGroup_propAt(pThis, (size_t)pThis->nEnable);
}

const char* DasSrcGroup_xorGroup(const DasSrcGroup* pThis){ return pThis->sXor; }
size_t DasSrcGroup_numMimes(const DasSrcGroup* pThis){ return pThis->uMime; }

const char* DasSrcGroup_mimeAt(const DasSrcGroup* pThis, size_t uIdx)
{
	return (uIdx < pThis->uMime) ? pThis->aMime[uIdx] : NULL;
}

size_t DasSrcGroup_numProps(const DasSrcGroup* pThis){ return pThis->uCount; }

const DasSrcProp* DasSrcGroup_prop(const DasSrcGroup* pThis, const char* sName)
{
	for(size_t u = 0; u < pThis->uCount; ++u){
		const DasSrcProp* p = DasSrcGroup_propAt(pThis, u);
		if(_das_src_propEq(p->sName, sName)) return p;
	}
	return NULL;
}

size_t DasSrcGroup_numCtrls(const DasSrcGroup* pThis)
{
	size_t n = 0;
	for(size_t u = 0; u < pThis->uCount; ++u)
		if(DasSrcGroup_propAt(pThis, u)->pCtrl != NULL) ++n;
	return n;
}

/* ************************************************************************* */
/* Properties */

size_t DasSrc_numProps(const DasSrc* pThis){ return pThis->uProp; }

const DasSrcProp* DasSrc_propAt(const DasSrc* pThis, size_t uIdx)
{
	return (uIdx < pThis->uProp) ? pThis->aProp + uIdx : NULL;
}

das_srcsec DasSrcProp_section(const DasSrcProp* pThis){ return pThis->sec; }
const DasSrcGroup* DasSrcProp_group(const DasSrcProp* pThis){ return pThis->pGroup; }
const char* DasSrcProp_name(const DasSrcProp* pThis){ return pThis->sName; }
const char* DasSrcProp_key(const DasSrcProp* pThis){ return pThis->sKey; }
das_srcproptype DasSrcProp_type(const DasSrcProp* pThis){ return pThis->type; }
const char* DasSrcProp_typeName(const DasSrcProp* pThis){ return pThis->sTypeName; }
const char* DasSrcProp_value(const DasSrcProp* pThis){ return pThis->sValue; }
const DasSrcCtrl* DasSrcProp_ctrl(const DasSrcProp* pThis){ return pThis->pCtrl; }

bool DasSrcProp_multi(const DasSrcProp* pThis, size_t* puMin, size_t* puMax)
{
	if(puMin) *puMin = pThis->uMultiLo;
	if(puMax) *puMax = pThis->uMultiHi;
	return pThis->bMulti;
}

/* Units for display.  Current nodes put a units string on the property;
 * both layouts may carry a units property with a "value" at the group
 * level. */
const char* DasSrcProp_units(const DasSrcProp* pThis)
{
	const DasJdo* pU = _dassrc_jget(pThis->pJdo, "units");
	if(pU != NULL){
		if(pU->type == DASJSONTYPE_STR) return DasJdo_str(pU);
		if(_dassrc_jstr(pU, "value")) return _dassrc_jstr(pU, "value");
	}
	if(pThis->sec == DASSRCSEC_COORD){
		const char* s = _dassrc_jstr(_dassrc_groupProps(pThis->pGroup->pJdo), "units/value");
		if(s != NULL) return s;
	}
	if(pThis->type == DASSRCPROPTYPE_TIME) return "UTC";
	return "";
}

const char* DasSrcProp_title(const DasSrcProp* pThis, char* sBuf, size_t uLen)
{
	const char* s = _dassrc_jstr(pThis->pJdo, "title");
	if(s == NULL) s = _dassrc_jstr(pThis->pJdo, "summary");
	if(s == NULL) s = _dassrc_jstr(pThis->pJdo, "label");
	if(s == NULL) s = _dassrc_jstr(pThis->pJdo, "name");
	if(s != NULL) return s;
	if(pThis->sec == DASSRCSEC_OPTION) return "";

	const char* sGroup = DasSrcGroup_label(pThis->pGroup);
	const char* sProp = _das_src_propName(pThis->sName);
	if(strcmp(sProp, "enabled") == 0){
		if(pThis->sec == DASSRCSEC_FORMAT){
			snprintf(sBuf, uLen, "Output as %s", sGroup);
		}
		else{
			/* A variable's title is usually the physical quantity, which
			   reads better after "Output" than its short label does */
			const char* sTitle = _dassrc_jstr(pThis->pGroup->pJdo, "title");
			snprintf(sBuf, uLen, "Output %s", sTitle ? sTitle : sGroup);
		}
		return sBuf;
	}
	if(pThis->sec == DASSRCSEC_COORD){
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
bool DasSrcCtrl_isRequired(const DasSrcCtrl* pThis){ return pThis->bRequired; }
const char* DasSrcCtrl_param(const DasSrcCtrl* pThis){ return pThis->sParam; }
size_t DasSrcCtrl_numEnums(const DasSrcCtrl* pThis){ return pThis->uEnum; }

const char* DasSrcCtrl_enumAt(const DasSrcCtrl* pThis, size_t uIdx)
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

/* One value, or one item of a list, against the property's type */
static das_errcode _DasSrcCtrl_checkOne(const DasSrcCtrl* pThis, const char* sVal, char* sWhy, size_t uWhy)
{
	const char* sKey = pThis->pProp->sKey;
	char* pEnd = NULL;

	switch(pThis->pProp->type){
	case DASSRCPROPTYPE_BOOL:{
		bool b;
		if(_dassrc_parseBool(sVal, &b)) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a boolean value for %s, use true or false", sVal, sKey);
		return DASERR_SRC;
	}
	case DASSRCPROPTYPE_ENUM:{
		for(size_t u = 0; u < pThis->uEnum; ++u)
			if(_dassrc_strieq(pThis->aEnum[u], sVal)) return DAS_OKAY;
		size_t u = snprintf(sWhy, uWhy, "'%s' is not an allowed value for %s, choose one of:", sVal, sKey);
		for(size_t v = 0; v < pThis->uEnum; ++v)
			u = _dassrc_catf(sWhy, uWhy, u, "%s %s", v ? "," : "", pThis->aEnum[v]);
		return DASERR_SRC;
	}
	case DASSRCPROPTYPE_INT:
		strtol(sVal, &pEnd, 10);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not an integer value for %s", sVal, sKey);
		return DASERR_SRC;
	case DASSRCPROPTYPE_REAL:
		strtod(sVal, &pEnd);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a real value for %s", sVal, sKey);
		return DASERR_SRC;
	case DASSRCPROPTYPE_TIME:{
		/* "now" is how nodes spell an open ended range and servers take
		   it in requests too */
		DasTime dt;
		if(_dassrc_strieq(sVal, "now") || DasTime_fromStr(&dt, sVal)) return DAS_OKAY;
		snprintf(sWhy, uWhy, "'%s' is not a time value for %s, use ISO 8601 such as "
		         "2026-01-15T12:00", sVal, sKey);
		return DASERR_SRC;
	}
	default:
		return DAS_OKAY;
	}
}

das_errcode DasSrcCtrl_check(const DasSrcCtrl* pThis, const char* sVal, char* sWhy, size_t uWhy)
{
	if(sVal == NULL) sVal = "";
	if(sWhy && uWhy) sWhy[0] = '\0';
	char sDummy[1];
	if(sWhy == NULL){ sWhy = sDummy; uWhy = 1; }
	const DasSrcProp* p = pThis->pProp;
	if(!p->bMulti) return _DasSrcCtrl_checkOne(pThis, sVal, sWhy, uWhy);

	size_t uItems = 0;
	char** aItem = _dassrc_newSplit(sVal, &uItems);
	if(aItem == NULL){ snprintf(sWhy, uWhy, "Out of memory"); return DASERR_SRC; }
	das_errcode nRet = DAS_OKAY;
	for(size_t u = 0; (nRet == DAS_OKAY) && (u < uItems); ++u){
		if(aItem[u][0] == '\0'){
			snprintf(sWhy, uWhy, "'%s' has an empty item, %s takes values separated by "
			         "single commas", sVal, p->sKey);
			nRet = DASERR_SRC;
			break;
		}
		nRet = _DasSrcCtrl_checkOne(pThis, aItem[u], sWhy, uWhy);
		/* Enum items are matched without regard to case, so Ex and ex are
		   one item.  Free text goes to the server as typed, a and A may be
		   two things there. */
		for(size_t v = 0; (nRet == DAS_OKAY) && (v < u); ++v){
			bool bSame = (pThis->uEnum > 0) ? _dassrc_strieq(aItem[u], aItem[v]) :
			                                  (strcmp(aItem[u], aItem[v]) == 0);
			if(!bSame) continue;
			snprintf(sWhy, uWhy, "'%s' is given twice for %s", aItem[u], p->sKey);
			nRet = DASERR_SRC;
		}
	}
	/* No items is nothing selected, which is not a count to bound */
	if((nRet == DAS_OKAY) && (uItems > 0) && ((uItems < p->uMultiLo)||(uItems > p->uMultiHi))){
		if(p->uMultiHi == SIZE_MAX)
			snprintf(sWhy, uWhy, "%s takes at least %zu values, %zu given", p->sKey, p->uMultiLo, uItems);
		else if(p->uMultiLo == p->uMultiHi)
			snprintf(sWhy, uWhy, "%s takes %zu values, %zu given", p->sKey, p->uMultiLo, uItems);
		else
			snprintf(sWhy, uWhy, "%s takes %zu to %zu values, %zu given", p->sKey,
			         p->uMultiLo ? p->uMultiLo : 1, p->uMultiHi, uItems);
		nRet = DASERR_SRC;
	}
	free(aItem);
	return nRet;
}

/* ************************************************************************* */
/* Key lookup */

int _DasSrc_match(const DasSrc* pThis, const char* sKey, const DasSrcProp** aOut, int nMax)
{
	char aParts[3][DASID_SZ];
	int nParts = _das_src_splitKey(sKey, aParts);
	if(nParts == 0) return 0;

	int n = 0;
	das_srcsec sec;
	const DasSrcProp* p = NULL;

#define _ADD_MATCH if(n < nMax) aOut[n] = p; ++n;

	if(nParts == 3){
		if(!_das_src_secByName(aParts[0], &sec)) return 0;
		for(size_t u = 0; u < pThis->uProp; ++u){
			p = pThis->aProp + u;
			if((p->sec == sec) && _dassrc_strieq(p->pGroup->sName, aParts[1]) &&
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
				if((sec == DASSRCSEC_OPTION) && _das_src_propEq(p->sName, aParts[1])){ _ADD_MATCH }
				else if((sec != DASSRCSEC_OPTION) && _dassrc_strieq(p->pGroup->sName, aParts[1]) &&
				        _das_src_propEq(p->sName, "enabled")){ _ADD_MATCH }
			}
			if(n > 0) return n;
		}
		for(size_t u = 0; u < pThis->uProp; ++u){
			p = pThis->aProp + u;
			if(_dassrc_strieq(p->pGroup->sName, aParts[0]) && _das_src_propEq(p->sName, aParts[1])){ _ADD_MATCH }
		}
		return n;
	}

	/* One part: an option, or a variable/format toggle */
	for(size_t u = 0; u < pThis->uProp; ++u){
		p = pThis->aProp + u;
		if((p->sec == DASSRCSEC_OPTION) && _das_src_propEq(p->sName, aParts[0])){ _ADD_MATCH }
		else if((p->sec != DASSRCSEC_OPTION) && _dassrc_strieq(p->pGroup->sName, aParts[0]) &&
		        _das_src_propEq(p->sName, "enabled")){ _ADD_MATCH }
	}
	if(n > 0) return n;

	/* Last resort, a bare property name.  Unique in practice for things
	   like 'serial', ambiguous for 'fracSecs' shared by das and csv, and
	   the ambiguity message then lists the full keys. */
	for(size_t u = 0; u < pThis->uProp; ++u){
		p = pThis->aProp + u;
		if((p->sec != DASSRCSEC_OPTION) && _das_src_propEq(p->sName, aParts[0])){ _ADD_MATCH }
	}
#undef _ADD_MATCH
	return n;
}

const DasSrcGroup* _DasSrc_coordGroup(const DasSrc* pThis, const char* sKey)
{
	char aParts[3][DASID_SZ];
	int nParts = _das_src_splitKey(sKey, aParts);
	const char* sGroup = NULL;
	das_srcsec sec;
	if(nParts == 1) sGroup = aParts[0];
	else if((nParts == 2) && _das_src_secByName(aParts[0], &sec) && (sec == DASSRCSEC_COORD))
		sGroup = aParts[1];
	else return NULL;
	return DasSrc_group(pThis, DASSRCSEC_COORD, sGroup);
}

#define MAX_MATCH 8

static void _dassrc_ambiguous(const char* sKey, const DasSrcProp** aMatch, int n, char* sWhy, size_t uWhy)
{
	size_t u = snprintf(sWhy, uWhy, "Key '%s' is ambiguous, use one of:", sKey);
	for(int i = 0; (i < n) && (i < MAX_MATCH); ++i)
		u = _dassrc_catf(sWhy, uWhy, u, " %s", aMatch[i]->sKey);
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
	else _dassrc_ambiguous(sKey, aMatch, n, sWhy, uWhy);
	return NULL;
}

/* ************************************************************************* */
/* Requisitions */

size_t DasSrc_numExamples(const DasSrc* pThis){ return pThis->uExample; }

const char* DasSrc_exampleLblAt(const DasSrc* pThis, size_t uIdx)
{
	return (uIdx < pThis->uExample) ? DasReq_label(pThis->aExample[uIdx]) : NULL;
}

/* ************************************************************************* */
/* Exclusive sets */

/* Groups whose enable toggles carry the same "xorGroup" name are radio
 * buttons: the formats of nearly every node, but any groups may be.  A
 * node writes each member as an ordinary boolean that knows nothing of
 * the others, so two could be switched on and both would send, or the
 * settings of a format not chosen would travel with the one that was.
 * Servers take such requests and do something with them, which is worse
 * than refusing.  So what a requisition says of the members is settled
 * here, once, and DasSrc_check(), DasSrc_reqUrl() and DasSrc_mime() all
 * read the result:
 *
 *   - The last member a requisition switches on is on, the others are
 *     off, whatever they default to and whatever was said of them
 *     before.  (Entry order is the order of saying, see DasReq_set().)
 *   - If it switches none on, the node's defaults stand, less any member
 *     it switches off.
 *
 * One state per group of the source, in group order. */
#define XOR_NONE  0   /* not a member of any exclusive set */
#define XOR_OFF   1
#define XOR_ON    2

#define SAID_NOTHING 0
#define SAID_OFF     1
#define SAID_ON      2

typedef struct {
	char cState;   /* XOR_* */
	char cSaid;    /* SAID_*, the requisition's last word on the toggle */
	bool bDefOn;   /* on in the node */
	size_t uAt;    /* the entry that said it */
} _xor_t;

static bool _DasSrcProp_isEnable(const DasSrcProp* p)
{
	const DasSrcGroup* pG = p->pGroup;
	return (pG->nEnable >= 0) && (p == pG->pSrc->aProp + pG->uFirst + (size_t)pG->nEnable);
}

/* aX has one zeroed item per group.  pReq may be NULL for the node's own
 * defaults, and need not have been checked. */
static void _DasSrc_xorStates(const DasSrc* pThis, const DasReq* pReq, _xor_t* aX)
{
	for(size_t g = 0; g < pThis->uGroup; ++g){
		const DasSrcGroup* pG = pThis->aGroup + g;
		if(pG->sXor == NULL) continue;
		bool b = false;
		const DasSrcProp* pOn = DasSrcGroup_enableProp(pG);
		aX[g].bDefOn = (pOn != NULL) && _dassrc_parseBool(pOn->sValue, &b) && b;
		aX[g].cState = aX[g].bDefOn ? XOR_ON : XOR_OFF;
	}

	for(size_t u = 0; (pReq != NULL) && (u < pReq->uEntry); ++u){
		const struct das_req_entry* pE = pReq->aEntry + u;
		if(pE->bDerived || (_DasSrc_coordGroup(pThis, pE->sKey) != NULL)) continue;
		const DasSrcProp* aMatch[1];
		if(_DasSrc_match(pThis, pE->sKey, aMatch, 1) != 1) continue;
		size_t g = (size_t)(aMatch[0]->pGroup - pThis->aGroup);
		if((aX[g].cState == XOR_NONE) || !_DasSrcProp_isEnable(aMatch[0])) continue;
		bool b = false;
		if(pE->sVal[0] == '\0') aX[g].cSaid = SAID_NOTHING;   /* took it back */
		else if(_dassrc_parseBool(pE->sVal, &b)) aX[g].cSaid = b ? SAID_ON : SAID_OFF;
		else continue;                                        /* check refuses it */
		aX[g].uAt = u;
	}

	for(size_t g = 0; g < pThis->uGroup; ++g){
		if(aX[g].cState == XOR_NONE) continue;
		const char* sSet = pThis->aGroup[g].sXor;
		bool bWinner = false, bMe = (aX[g].cSaid == SAID_ON);
		for(size_t h = 0; h < pThis->uGroup; ++h){
			if((h == g) || (aX[h].cSaid != SAID_ON) || (pThis->aGroup[h].sXor == NULL)) continue;
			if(strcmp(pThis->aGroup[h].sXor, sSet) != 0) continue;
			bWinner = true;
			if(aX[h].uAt > aX[g].uAt) bMe = false;
		}
		if(bMe) aX[g].cState = XOR_ON;
		else if(bWinner || (aX[g].cSaid == SAID_OFF)) aX[g].cState = XOR_OFF;
	}
}

static _xor_t* _DasSrc_newXorStates(const DasSrc* pThis, const DasReq* pReq)
{
	_xor_t* aX = (_xor_t*)calloc(pThis->uGroup ? pThis->uGroup : 1, sizeof(_xor_t));
	if(aX != NULL) _DasSrc_xorStates(pThis, pReq, aX);
	return aX;
}

/* A property that has no say in this request: any but the toggle of a
 * member that is off. */
static bool _DasSrcProp_xorIdle(const DasSrc* pThis, const _xor_t* aX, const DasSrcProp* p)
{
	return (aX[p->pGroup - pThis->aGroup].cState == XOR_OFF) && !_DasSrcProp_isEnable(p);
}

/* True if some member of the set g belongs to is on */
static bool _DasSrc_xorAnyOn(const DasSrc* pThis, const _xor_t* aX, size_t g)
{
	for(size_t h = 0; h < pThis->uGroup; ++h){
		if((aX[h].cState != XOR_ON) || (strcmp(pThis->aGroup[h].sXor, pThis->aGroup[g].sXor) != 0)) continue;
		return true;
	}
	return false;
}

/* The key of a property's group, the property's key less its last part */
static void _DasSrcProp_groupKey(const DasSrcProp* p, char* sBuf, size_t uLen)
{
	_dassrc_strcpyz(sBuf, p->sKey, uLen);
	char* pDot = strrchr(sBuf, '.');
	if(pDot != NULL) *pDot = '\0';
}

void DasSrc_stampReq(const DasSrc* pThis, DasReq* pReq)
{
	DasReq_setSrc(pReq, pThis->sPath, pThis->sUrl, DasSrc_version(pThis));
}

DasReq* new_DasReq_src(const DasSrc* pThis, int nExample)
{
	if(nExample >= 0){
		if((size_t)nExample >= pThis->uExample){
			daslog_errorf("Source %s has %zu example(s), there is no example %d",
			               pThis->sUrl, pThis->uExample, nExample);
			return NULL;
		}
		return new_DasReq_copy(pThis->aExample[nExample]);
	}
	DasReq* pReq = new_DasReq();
	_xor_t* aX = _DasSrc_newXorStates(pThis, NULL);
	if((pReq == NULL)||(aX == NULL)){ free(aX); del_DasReq(pReq); return NULL; }
	DasSrc_stampReq(pThis, pReq);
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		if((p->pCtrl == NULL)||!p->pCtrl->bRequired||(p->sValue != NULL)) continue;
		if(_DasSrcProp_xorIdle(pThis, aX, p)) continue;   /* asked for when chosen */
		if(DasReq_set(pReq, p->sKey, "") != DAS_OKAY){ free(aX); del_DasReq(pReq); return NULL; }
	}
	free(aX);
	return pReq;
}

/* Identity is the catalog URI alone.  The URL is where one copy of the
 * node happened to come from; a mirror or a local mount of the same node
 * has a different one and must validate the same requisitions. */
static bool _DasSrc_stampMatches(const DasSrc* pThis, const DasReq* pReq)
{
	const char* sPath = DasReq_srcPath(pReq);
	if(sPath[0] == '\0') return true;   /* unstamped */
	return _dassrc_strieq(sPath, pThis->sPath);
}

/* One item of a coordinate range: find the control, check the value,
 * add the derived entry.  Returns false with sWhy filled on any trouble. */
static bool _dassrc_rangeItem(
	const DasSrc* pThis, DasReq* pReq, const DasSrcGroup* pG, const char* sProp,
	const char* sVal, bool* aPresent, bool* aCovered, char* sWhy, size_t uWhy
){
	const DasSrcProp* p = DasSrcGroup_prop(pG, sProp);
	if((p == NULL)||(p->pCtrl == NULL)){
		snprintf(sWhy, uWhy, "Coordinate %s has no settable '%s' property", pG->sName, sProp);
		return false;
	}
	if(DasSrcCtrl_check(p->pCtrl, sVal, sWhy, uWhy) != DAS_OKAY) return false;
	int n = _DasReq_add(pReq, p->sKey, sVal, true);
	if(n < 0){ snprintf(sWhy, uWhy, "Out of memory"); return false; }
	size_t uProp = (size_t)(p - pThis->aProp);
	pReq->aEntry[n].nProp = (int)uProp;
	_DasReq_setVerdict(pReq, (size_t)n, DASREQVERDICT_OK, NULL);
	aPresent[uProp] = true;
	aCovered[uProp] = true;
	return true;
}

/* GROUP=MIN,MAX[,STEP] or GROUP=SPAN, see "Keys" in source.h */
static bool _dassrc_rangeEntry(
	const DasSrc* pThis, DasReq* pReq, const DasSrcGroup* pG, const char* sVal,
	bool* aPresent, bool* aCovered, char* sWhy, size_t uWhy
){
	if(strchr(sVal, ',') == NULL){
		/* a single time denotes its span */
		const DasSrcProp* pMin = DasSrcGroup_prop(pG, "min");
		if((pMin == NULL)||(pMin->type != DASSRCPROPTYPE_TIME)){
			snprintf(sWhy, uWhy, "Give coordinate %s a range as MIN,MAX", pG->sName);
			return false;
		}
		DasTime dtBeg, dtEnd;
		if(!DasTime_strToSpan(sVal, &dtBeg, &dtEnd)){
			snprintf(sWhy, uWhy, "'%s' is neither a MIN,MAX range nor a time span such "
			         "as 2026-01-02 or 2026-01-02T14", sVal);
			return false;
		}
		char sBeg[40], sEnd[40];
		DasTime_toIsoC(&dtBeg, 0, sBeg, sizeof(sBeg));
		DasTime_toIsoC(&dtEnd, 0, sEnd, sizeof(sEnd));
		return _dassrc_rangeItem(pThis, pReq, pG, "min", sBeg, aPresent, aCovered, sWhy, uWhy) &&
		       _dassrc_rangeItem(pThis, pReq, pG, "max", sEnd, aPresent, aCovered, sWhy, uWhy);
	}

	const char* aProps[3] = {"min", "max", NULL};
	aProps[2] = DasSrcGroup_prop(pG, "res") ? "res" : "inter";

	char sCopy[SCALAR_SZ];
	_dassrc_strcpyz(sCopy, sVal, sizeof(sCopy));
	char* pTok = sCopy;
	for(int i = 0; i < 4; ++i){
		char* pComma = strchr(pTok, ',');
		if(pComma) *pComma = '\0';
		if(i == 3){
			snprintf(sWhy, uWhy, "Too many values for coordinate %s, at most MIN,MAX,STEP", pG->sName);
			return false;
		}
		if(pTok[0] != '\0')
			if(!_dassrc_rangeItem(pThis, pReq, pG, aProps[i], pTok, aPresent, aCovered, sWhy, uWhy))
				return false;
		if(pComma == NULL) break;
		pTok = pComma + 1;
	}
	return true;
}

das_errcode DasSrc_check(const DasSrc* pThis, DasReq* pReq, size_t* puBad)
{
	if(puBad) *puBad = 0;
	if(!_DasSrc_stampMatches(pThis, pReq)){
		daslog_errorf("Requisition is for source %s%s, not %s%s",
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
	_xor_t* aX = _DasSrc_newXorStates(pThis, pReq);
	if(aX == NULL){ free(aPresent); return DASERR_SRC; }

	size_t uBad = 0;
	char sWhy[1024];
	char sGroup[256];
	size_t uClient = pReq->uEntry;   /* derived entries are appended past here */
	for(size_t u = 0; u < uClient; ++u){
		const char* sKey = pReq->aEntry[u].sKey;
		const char* sVal = pReq->aEntry[u].sVal;

		const DasSrcGroup* pG = _DasSrc_coordGroup(pThis, sKey);
		if(pG != NULL){
			if(_dassrc_rangeEntry(pThis, pReq, pG, sVal, aPresent, aCovered, sWhy, sizeof(sWhy)))
				_DasReq_setVerdict(pReq, u, DASREQVERDICT_OK, NULL);
			else{
				_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy);
				++uBad;
			}
			continue;
		}

		const DasSrcProp* aMatch[MAX_MATCH];
		int n = _DasSrc_match(pThis, sKey, aMatch, MAX_MATCH);
		if(n == 0){
			snprintf(sWhy, sizeof(sWhy), "No property named '%s' in this source", sKey);
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}
		if(n > 1){
			_dassrc_ambiguous(sKey, aMatch, n, sWhy, sizeof(sWhy));
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}
		const DasSrcProp* p = aMatch[0];
		size_t uP = (size_t)(p - pThis->aProp);
		pReq->aEntry[u].nProp = (int)uP;
		if(p->pCtrl == NULL){
			snprintf(sWhy, sizeof(sWhy), "%s is a fixed property of this source and cannot be set", p->sKey);
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}
		aPresent[uP] = true;

		/* A setting of a format that is not the one chosen, or of any
		   member of an exclusive set that is off.  Sending it would be a
		   request no form could have made; dropping it would hide that
		   the user and the request disagree. */
		size_t uG = (size_t)(p->pGroup - pThis->aGroup);
		if(_DasSrcProp_xorIdle(pThis, aX, p)){
			if(_dassrc_noValue(sVal, p->bMulti)){
				_DasReq_setVerdict(pReq, u, DASREQVERDICT_OK, NULL);
				continue;
			}
			_DasSrcProp_groupKey(p, sGroup, sizeof(sGroup));
			snprintf(sWhy, sizeof(sWhy), "%s is a setting of %s, which is not selected.  Select it "
			         "with %s=true, or take the setting back with %s=", p->sKey, sGroup, sGroup, sKey);
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}
		/* Radio buttons have no "none": switching off the member that is
		   on, and no other on, would send nothing and the server would
		   answer with the very thing that was switched off */
		if((aX[uG].cSaid == SAID_OFF) && (aX[uG].uAt == u) && aX[uG].bDefOn &&
		   _DasSrcProp_isEnable(p) && !_DasSrc_xorAnyOn(pThis, aX, uG)){
			size_t n = snprintf(sWhy, sizeof(sWhy), "%s cannot be switched off by itself, one of "
			         "the '%s' choices is always on.  Switch on one of:", p->sKey, p->pGroup->sXor);
			int nOthers = 0;
			for(size_t h = 0; h < pThis->uGroup; ++h){
				const DasSrcProp* pOn = DasSrcGroup_enableProp(pThis->aGroup + h);
				if((h == uG)||(aX[h].cState == XOR_NONE)||(pOn == NULL)) continue;
				if(strcmp(pThis->aGroup[h].sXor, p->pGroup->sXor) != 0) continue;
				_DasSrcProp_groupKey(pOn, sGroup, sizeof(sGroup));
				n = _dassrc_catf(sWhy, sizeof(sWhy), n, "%s %s", nOthers ? "," : "", sGroup);
				++nOthers;
			}
			if(nOthers == 0)
				snprintf(sWhy, sizeof(sWhy), "%s cannot be switched off, it is the only '%s' "
				         "choice this source has", p->sKey, p->pGroup->sXor);
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}

		if(_dassrc_noValue(sVal, p->bMulti)){
			if(p->pCtrl->bRequired && (p->sValue == NULL)){
				snprintf(sWhy, sizeof(sWhy), "%s is required, give it a value", p->sKey);
				_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad;
			}
			else
				_DasReq_setVerdict(pReq, u, DASREQVERDICT_OK, NULL);
			continue;
		}
		if(DasSrcCtrl_check(p->pCtrl, sVal, sWhy, sizeof(sWhy)) != DAS_OKAY){
			_DasReq_setVerdict(pReq, u, DASREQVERDICT_BAD, sWhy); ++uBad; continue;
		}
		aCovered[uP] = true;
		_DasReq_setVerdict(pReq, u, DASREQVERDICT_OK, NULL);
	}

	/* An empty entry for a required control is fine after all when a range
	   entry covered the same control: the blank requisition lists
	   coords.time.min, the user typed time=A,B, both name one property */
	for(size_t u = 0; u < uClient; ++u){
		struct das_req_entry* pE = pReq->aEntry + u;
		if((pE->verdict != DASREQVERDICT_BAD)||(pE->nProp < 0)||(pE->sVal[0] != '\0')) continue;
		if(!aCovered[pE->nProp]) continue;
		_DasReq_setVerdict(pReq, u, DASREQVERDICT_OK, NULL);
		--uBad;
	}

	/* required controls nobody mentioned */
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		if((p->pCtrl == NULL)||!p->pCtrl->bRequired||(p->sValue != NULL)||aPresent[u]) continue;
		if(_DasSrcProp_xorIdle(pThis, aX, p)) continue;
		int n = _DasReq_add(pReq, p->sKey, "", true);
		if(n < 0) break;
		pReq->aEntry[n].nProp = (int)u;
		snprintf(sWhy, sizeof(sWhy), "%s is required, set it with %s=VALUE", p->sKey, p->sKey);
		_DasReq_setVerdict(pReq, (size_t)n, DASREQVERDICT_BAD, sWhy);
		++uBad;
	}
	free(aPresent);
	free(aX);
	if(puBad) *puBad = uBad;
	return uBad ? DASERR_SRC : DAS_OKAY;
}

/* The format group a requisition turns on, else the node's default.
 * Formats in an exclusive set, the usual case, are settled by
 * _DasSrc_xorStates(); nodes from before xorGroup go by the first format
 * switched on. */
static const DasSrcGroup* _DasSrc_chosenFormat(const DasSrc* pThis, const DasReq* pReq)
{
	_xor_t* aX = _DasSrc_newXorStates(pThis, pReq);
	if(aX == NULL) return NULL;
	const DasSrcGroup* pDef = NULL;
	const DasSrcGroup* pXor = NULL;
	const DasSrcGroup* pAsked = NULL;
	for(size_t g = 0; (pAsked == NULL) && (g < DasSrc_numGroups(pThis, DASSRCSEC_FORMAT)); ++g){
		const DasSrcGroup* pG = DasSrc_groupAt(pThis, DASSRCSEC_FORMAT, g);
		const DasSrcProp* pOn = DasSrcGroup_enableProp(pG);
		if(pOn == NULL) continue;
		if(pG->sXor != NULL){
			if((pXor == NULL) && (aX[pG - pThis->aGroup].cState == XOR_ON)) pXor = pG;
			continue;
		}
		bool b = false;
		if((pDef == NULL) && _dassrc_parseBool(pOn->sValue, &b) && b) pDef = pG;
		if(pReq == NULL) continue;
		for(size_t u = 0; u < pReq->uEntry; ++u){
			if(pReq->aEntry[u].bDerived) continue;
			const DasSrcProp* aMatch[1];
			if(_DasSrc_match(pThis, pReq->aEntry[u].sKey, aMatch, 1) != 1) continue;
			if((aMatch[0] == pOn) && _dassrc_parseBool(pReq->aEntry[u].sVal, &b) && b) pAsked = pG;
		}
	}
	free(aX);
	if(pAsked != NULL) return pAsked;
	return pXor ? pXor : pDef;
}

const char* DasSrc_mime(const DasSrc* pThis, const DasReq* pReq)
{
	const DasSrcGroup* pG = _DasSrc_chosenFormat(pThis, pReq);
	if((pG != NULL) && (pG->uMime > 0)) return pG->aMime[0];

	/* 2018 nodes state a default format outside the interface.  2018 layout,
	   down to the return. */
	const char* s = _dassrc_jstr(pThis->pDoc, "format/default/mime");
	if(s == NULL) s = _dassrc_jstr(pThis->pDoc, "protocol/format/default/mime");
	if(s == NULL) s = _dassrc_jstr(pThis->pDoc, "interface/format/default/mime");
	return s ? s : "";
}

/* ************************************************************************* */
/* Stream sources: translating a requisition to protocol parameters */

static const DasStreamSrc* _DasSrc_stream(const DasSrc* pThis)
{
	return (pThis->kind == DASSRCKIND_STREAM) ? (const DasStreamSrc*)pThis : NULL;
}

size_t DasSrc_numBaseUrls(const DasSrc* pThis)
{
	const DasStreamSrc* pS = _DasSrc_stream(pThis);
	return pS ? _dassrc_jlen(pS->pBaseUrls) : 0;
}

const char* DasSrc_baseUrlAt(const DasSrc* pThis, size_t uIdx)
{
	const DasStreamSrc* pS = _DasSrc_stream(pThis);
	if(pS == NULL) return NULL;
	const DasJsonAryEl* pEl = DasJdo_aryFirst(pS->pBaseUrls);
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	return pEl ? DasJdo_str(pEl->value) : NULL;
}

/* The output parameter list.  Names are borrowed from the document or the
 * requisition.  A flag set parameter collects one piece per flag sent and
 * is joined with its flagSep when the URL is built, not as controls are
 * visited, because a flag with a subSep gathers elements from controls
 * anywhere in the node into one piece.  A plain value set afterwards
 * replaces the pieces, as a raw parameter replaces what controls built.
 *
 * Pieces are kept in the order the parameter's "flags" object lists the
 * flags, not the order controls are visited in.  What is sent is then a
 * matter of the protocol section alone: a node may move a control from
 * the options to a data variable, or reorder a form, and every request
 * it makes stays byte for byte what it was.  Servers that cache by the
 * request text need that.  Pieces of one flag (a flag that repeats) keep
 * the order they came in.
 *
 * A parameter that is not a flag set takes several values the same way:
 * with its own subSep it is a single merged piece, keyed by the parameter
 * entry where a flag's piece is keyed by the flag entry; with "repeat"
 * every value is a piece and each goes out as its own NAME=VALUE pair. */
typedef struct {
	const DasJdo* pFlag;  /* merge key, the flag's entry or the parameter's;
	                         NULL when not merged */
	size_t uRank;         /* the flag's place among the parameter's flags */
	char* sVal;           /* owned, prefix then element(s) */
	const char* sSuffix;  /* borrowed from the document, what closes the
	                         piece once every element is in, or NULL */
} _piece_t;

typedef struct {
	const char* sName;
	char* sVal;           /* owned, plain value, NULL when only pieces */
	const char* sFlagSep; /* borrowed from the document */
	bool bRepeat;         /* pieces are separate pairs, not one joined value */
	_piece_t* aPiece; size_t uPiece; size_t uCap;
} _param_t;

typedef struct { _param_t* a; size_t u; size_t uCap; } _out_t;

static _param_t* _dassrc_outFind(_out_t* pO, const char* sName)
{
	for(size_t u = 0; u < pO->u; ++u)
		if(strcmp(pO->a[u].sName, sName) == 0) return pO->a + u;
	return NULL;
}

static _param_t* _dassrc_outGet(_out_t* pO, const char* sName)
{
	_param_t* p = _dassrc_outFind(pO, sName);
	if(p != NULL) return p;
	if(pO->u == pO->uCap){
		size_t uCap = pO->uCap ? pO->uCap * 2 : 16;
		_param_t* a = (_param_t*)realloc(pO->a, uCap * sizeof(_param_t));
		if(a == NULL) return NULL;
		pO->a = a;
		pO->uCap = uCap;
	}
	p = pO->a + pO->u;
	memset(p, 0, sizeof(_param_t));
	p->sName = sName;
	pO->u += 1;
	return p;
}

static void _dassrc_outDropPieces(_param_t* p)
{
	for(size_t u = 0; u < p->uPiece; ++u) free(p->aPiece[u].sVal);
	free(p->aPiece);
	p->aPiece = NULL;
	p->uPiece = 0;
	p->uCap = 0;
	p->bRepeat = false;
}

static bool _dassrc_outSet(_out_t* pO, const char* sName, const char* sVal)
{
	_param_t* p = _dassrc_outGet(pO, sName);
	if(p == NULL) return false;
	char* sNew = _dassrc_strdupz(sVal);
	if(sNew == NULL) return false;
	free(p->sVal);
	p->sVal = sNew;
	_dassrc_outDropPieces(p);
	return true;
}

/* Send a flag.  With sSubSep, a second element for the same flag joins
 * the first piece instead of starting another.  Without it every call is
 * a piece of its own, which is how a flag that repeats sends a list:
 * prefix and item, flagSep, prefix and item.  A suffix is kept apart
 * from the text until the value is written, since a joined piece is not
 * finished when it is started. */
static bool _dassrc_outAddFlag(
	_out_t* pO, const char* sName, const char* sFlagSep, const DasJdo* pFlag,
	size_t uRank, const char* sPrefix, const char* sSuffix, const char* sSubSep,
	const char* sElem
){
	_param_t* p = _dassrc_outGet(pO, sName);
	if(p == NULL) return false;
	if(sFlagSep != NULL) p->sFlagSep = sFlagSep;

	if(sSubSep != NULL){
		for(size_t u = 0; u < p->uPiece; ++u){
			_piece_t* pP = p->aPiece + u;
			if(pP->pFlag != pFlag) continue;
			char* sNew = dasstr_newf("%s%s%s", pP->sVal, sSubSep, sElem);
			if(sNew == NULL) return false;
			free(pP->sVal);
			pP->sVal = sNew;
			return true;
		}
	}
	if(p->uPiece == p->uCap){
		size_t uCap = p->uCap ? p->uCap * 2 : 4;
		_piece_t* a = (_piece_t*)realloc(p->aPiece, uCap * sizeof(_piece_t));
		if(a == NULL) return false;
		p->aPiece = a;
		p->uCap = uCap;
	}
	char* sNew = dasstr_newf("%s%s", sPrefix ? sPrefix : "", sElem);
	if(sNew == NULL) return false;

	/* after every piece of the same or an earlier flag */
	size_t uAt = p->uPiece;
	while((uAt > 0) && (p->aPiece[uAt - 1].uRank > uRank)) --uAt;
	memmove(p->aPiece + uAt + 1, p->aPiece + uAt, (p->uPiece - uAt) * sizeof(_piece_t));
	_piece_t* pP = p->aPiece + uAt;
	pP->pFlag = sSubSep ? pFlag : NULL;
	pP->uRank = uRank;
	pP->sVal = sNew;
	pP->sSuffix = sSuffix;
	p->uPiece += 1;
	return true;
}

/* The value as sent: the plain value, if any, then each piece, joined
 * with flagSep.  Allocated, NULL when out of memory. */
static char* _dassrc_outJoin(const _param_t* p)
{
	const char* sSep = p->sFlagSep ? p->sFlagSep : " ";
	size_t uSep = strlen(sSep);
	size_t uLen = (p->sVal ? strlen(p->sVal) : 0) + 1;
	for(size_t u = 0; u < p->uPiece; ++u){
		uLen += uSep + strlen(p->aPiece[u].sVal);
		if(p->aPiece[u].sSuffix) uLen += strlen(p->aPiece[u].sSuffix);
	}

	char* s = (char*)malloc(uLen);
	if(s == NULL) return NULL;
	size_t n = 0;
	if(p->sVal){ strcpy(s, p->sVal); n = strlen(s); }
	for(size_t u = 0; u < p->uPiece; ++u){
		if(n > 0){ memcpy(s + n, sSep, uSep); n += uSep; }
		size_t uVal = strlen(p->aPiece[u].sVal);
		memcpy(s + n, p->aPiece[u].sVal, uVal);
		n += uVal;
		if(p->aPiece[u].sSuffix){
			uVal = strlen(p->aPiece[u].sSuffix);
			memcpy(s + n, p->aPiece[u].sSuffix, uVal);
			n += uVal;
		}
	}
	s[n] = '\0';
	return s;
}

static void _dassrc_outClear(_out_t* pO)
{
	for(size_t u = 0; u < pO->u; ++u){
		free(pO->a[u].sVal);
		_dassrc_outDropPieces(pO->a + u);
	}
	free(pO->a);
	memset(pO, 0, sizeof(_out_t));
}

/* Where a flag stands among its parameter's flags, counting from 0.
   The caller has already found the flag, so it is there. */
static size_t _dassrc_flagRank(const DasJdo* pParam, const char* sFlag)
{
	size_t uRank = 0;
	for(const DasJsonDictEl* pEl = DasJdo_dictFirst(_dassrc_jget(pParam, "flags")); pEl; pEl = pEl->next){
		if(strcmp(pEl->name->string, sFlag) == 0) break;
		++uRank;
	}
	return uRank;
}

/* The protocol's own list of what may follow a flag's prefix, or be the
 * value of a parameter: "enum" on the flag or parameter entry, a plain
 * list of strings.  The protocol section then names every string the
 * server takes there, and what a control sends (an interface enum item's
 * pval or value, a typed value) has to be one of them.  Compared as text
 * and exactly, the server will.  An entry with no list takes anything,
 * which is right for a time, a number or free text.  True when sVal may
 * be sent to pTarget.
 *
 * Not to be confused with a control's set.enum, the choices a person
 * sees.  Those may be other words; this list is the wire. */
static bool _dassrc_inProtoEnum(const DasJdo* pTarget, const char* sVal)
{
	const DasJdo* pVals = _dassrc_jget(pTarget, "enum");
	if(!_dassrc_jisary(pVals)) return true;
	char sItem[SCALAR_SZ];
	for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pVals); pEl; pEl = pEl->next)
		if(_das_json_scalar(pEl->value, sItem, sizeof(sItem)) && (strcmp(sItem, sVal) == 0))
			return true;
	return false;
}

/* Say why a value was refused by _dassrc_inProtoEnum().  A control with
   choices of its own can only get here if the node's two sections
   disagree; one without lets the user type, so the user is told. */
static void _dassrc_notInProtoEnum(
	const DasSrcCtrl* pC, const char* sVal, const char* sFlag, char* sWhy, size_t uWhy
){
	size_t n = 0;
	if(sFlag != NULL)
		n = _dassrc_catf(sWhy, uWhy, n, "%s would send '%s', which flag '%s' under protocol "
		                 "parameter %s does not list in its enum", pC->pProp->sKey, sVal, sFlag, pC->sParam);
	else
		n = _dassrc_catf(sWhy, uWhy, n, "%s would send '%s', which protocol parameter %s does "
		                 "not list in its enum", pC->pProp->sKey, sVal, pC->sParam);
	if(pC->uEnum > 0) _dassrc_catf(sWhy, uWhy, n, ", the node is broken");
}

/* What a control's value is joined into, if anything: the entry of a
 * flag with a subSep, or of a parameter of its own with one.  NULL for a
 * control whose value travels by itself.
 *
 * A joined value is one value as far as the server can tell, '-hfr ABEx'
 * or 'read.data=a,b', so its parts are sent together or not at all, see
 * DasSrc_reqUrl().  An enum whose items each pick a flag has no single
 * target and is not looked into; such items are switches. */
static const DasJdo* _DasSrcCtrl_joinTarget(const DasSrcCtrl* pC)
{
	const DasJdo* pT = pC->pParam;
	const char* sFlag = _dassrc_jstr(pC->pSet, "flag");
	if(sFlag != NULL){
		char sPath[DASID_SZ + 8];
		snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
		pT = _dassrc_jget(pC->pParam, sPath);
	}
	if((_dassrc_jstr(pT, "subSep") == NULL) || _dassrc_jtrue(_dassrc_jget(pT, "repeat"))) return NULL;
	return pT;
}

/* One control's value to protocol parameters, by its set block:
 *
 *   - enum: the chosen item's "pval" if it has one, else its value, and
 *     an item may name a flag
 *   - boolean: the block fires for one state only, "value" (true unless
 *     said otherwise); the other state is the default and sends nothing.
 *     With neither pval nor flag the state itself is sent as true/false.
 *   - flag: with the flag's prefix before it and the flag's suffix, if it
 *     has one, after it, appended to the parameter with the parameter's
 *     flagSep.  A switch sends the flag's value (its entry
 *     under the parameter's flags, else the flag name), a slot sends the
 *     control's value, see _DasSrcCtrl_flagIsSlot()
 *   - flag with a subSep (the das2 set: constraint): a multi-select.  A
 *     switch sends its pval, a slot its value, and everything sent to
 *     that flag is joined with subSep behind one prefix, in the order the
 *     controls appear in the node.  "" joins with nothing between.  (The
 *     flags themselves go out in the order the parameter lists them.)
 *   - flag with "repeat": true: each value is a flag of its own, prefix
 *     and all, which is what any flag without a subSep does when two
 *     controls feed it.  Saying so is required only of a list control,
 *     see _DasSrcCtrl_translate().
 *   - otherwise the value as given, or the block's pval, to a parameter
 *     of its own.  The parameter may carry subSep or repeat as a flag
 *     does, and then gathers values instead of keeping the last one.
 *   - a flag with a prefix, or a parameter of its own, may list an
 *     "enum" of its own.  Whatever is about to follow the prefix, or be
 *     the parameter's value or one element of it, must then be in that
 *     list, see _dassrc_inProtoEnum().
 */
static bool _DasSrcCtrl_translateOne(const DasSrcCtrl* pC, const char* sVal, _out_t* pO, char* sWhy, size_t uWhy)
{
	const DasSrcProp* p = pC->pProp;
	char sFlag[DASID_SZ] = {'\0'};
	char sPVal[SCALAR_SZ] = {'\0'};
	bool bSlot = _DasSrcCtrl_flagIsSlot(pC);
	_dassrc_strcpyz(sFlag, _dassrc_jstr(pC->pSet, "flag"), sizeof(sFlag));
	bool bHavePVal = _das_json_scalar(_dassrc_jget(pC->pSet, "pval"), sPVal, sizeof(sPVal));

	const DasJdo* pEnum = _dassrc_jget(pC->pSet, "enum");
	if(_dassrc_jisary(pEnum)){
		char sItem[SCALAR_SZ];
		for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
			const DasJdo* pItem = _dassrc_jisdict(pEl->value) ? _dassrc_jget(pEl->value, "value") : pEl->value;
			if(!_das_json_scalar(pItem, sItem, sizeof(sItem))) continue;
			if(!_dassrc_strieq(sItem, sVal)) continue;
			if(_das_json_scalar(_dassrc_jget(pEl->value, "pval"), sPVal, sizeof(sPVal))) bHavePVal = true;
			else if(!bHavePVal){ _dassrc_strcpyz(sPVal, sItem, sizeof(sPVal)); bHavePVal = true; }
			if(_dassrc_jstr(pEl->value, "flag")){
				_dassrc_strcpyz(sFlag, _dassrc_jstr(pEl->value, "flag"), sizeof(sFlag));
				bSlot = false;   /* the item picked a flag, it's a switch */
			}
			break;
		}
	}
	else if(p->type == DASSRCPROPTYPE_BOOL){
		bool bUser = false;
		_dassrc_parseBool(sVal, &bUser);
		const DasJdo* pTrig = _dassrc_jget(pC->pSet, "value");
		if(_dassrc_jisbool(pTrig) && (bUser != _dassrc_jtrue(pTrig))) return true;
		if(!bHavePVal && (sFlag[0] == '\0')){
			_dassrc_strcpyz(sPVal, bUser ? "true" : "false", sizeof(sPVal));
			bHavePVal = true;
		}
		/* A fixed string or a flag is sent for one state, and a block
		   that does not say which means on.  Off would otherwise send
		   the very string that means on. */
		else if(!_dassrc_jisbool(pTrig) && !bUser) return true;
	}

	if(sFlag[0] != '\0'){
		char sPath[DASID_SZ + 8];
		snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
		const DasJdo* pF = _dassrc_jget(pC->pParam, sPath);
		if(pF == NULL){
			snprintf(sWhy, uWhy, "Flag '%s' for %s is not defined under protocol parameter %s, "
			         "the node is broken", sFlag, p->sKey, pC->sParam);
			return false;
		}
		const char* sSubSep = _dassrc_jstr(pF, "subSep");
		bool bRepeat = _dassrc_jtrue(_dassrc_jget(pF, "repeat"));
		if(p->bMulti && bSlot && ((sSubSep == NULL) == !bRepeat)){
			/* Joined or repeated is a different request to the server, so
			   the node has to say which; guessing would hide a typo */
			snprintf(sWhy, uWhy, "%s holds a list, so flag '%s' under protocol parameter %s "
			         "must have exactly one of subSep and repeat, the node is broken",
			         p->sKey, sFlag, pC->sParam);
			return false;
		}
		if(bRepeat) sSubSep = NULL;
		char sFVal[SCALAR_SZ];
		if(bSlot)
			_dassrc_strcpyz(sFVal, bHavePVal ? sPVal : sVal, sizeof(sFVal));
		else if(sSubSep != NULL){
			/* Every switch on the flag would send the same flag value, so
			   the element has to come from the switch */
			if(!bHavePVal){
				snprintf(sWhy, uWhy, "Flag '%s' under protocol parameter %s joins elements, but "
				         "%s gives no pval for its element, the node is broken", sFlag, pC->sParam, p->sKey);
				return false;
			}
			_dassrc_strcpyz(sFVal, sPVal, sizeof(sFVal));
		}
		else if(!_das_json_scalar(_dassrc_jget(pF, "value"), sFVal, sizeof(sFVal)))
			_dassrc_strcpyz(sFVal, sFlag, sizeof(sFVal));
		/* A plain switch sends the flag's own text, there is nothing to
		   look up; anything that follows a prefix is held to the list */
		if((bSlot || (sSubSep != NULL)) && !_dassrc_inProtoEnum(pF, sFVal)){
			_dassrc_notInProtoEnum(pC, sFVal, sFlag, sWhy, uWhy);
			return false;
		}
		const char* sSep = _dassrc_jstr(pC->pParam, "flagSep");
		if(sSep == NULL) sSep = _dassrc_jstr(pC->pParam, "flag_sep");   /* 2018 layout */
		if(sSep == NULL) sSep = " ";
		if(!_dassrc_outAddFlag(pO, pC->sParam, sSep, pF, _dassrc_flagRank(pC->pParam, sFlag),
		                       _dassrc_jstr(pF, "prefix"), _dassrc_jstr(pF, "suffix"), sSubSep, sFVal)){
			snprintf(sWhy, uWhy, "Out of memory");
			return false;
		}
		return true;
	}

	/* A parameter of its own.  One that says how several values travel
	   collects them, from a list or from any number of controls; without
	   that the last value set is the value, as it always was. */
	const char* sSubSep = _dassrc_jstr(pC->pParam, "subSep");
	bool bRepeat = _dassrc_jtrue(_dassrc_jget(pC->pParam, "repeat"));
	if(p->bMulti && ((sSubSep == NULL) == !bRepeat)){
		snprintf(sWhy, uWhy, "%s holds a list, so protocol parameter %s must have exactly "
		         "one of subSep and repeat, the node is broken", p->sKey, pC->sParam);
		return false;
	}
	if(!_dassrc_inProtoEnum(pC->pParam, bHavePVal ? sPVal : sVal)){
		_dassrc_notInProtoEnum(pC, bHavePVal ? sPVal : sVal, NULL, sWhy, uWhy);
		return false;
	}
	bool bOk = true;
	if(bRepeat){
		bOk = _dassrc_outAddFlag(pO, pC->sParam, NULL, NULL, 0, NULL, NULL, NULL, bHavePVal ? sPVal : sVal);
		if(bOk) _dassrc_outFind(pO, pC->sParam)->bRepeat = true;
	}
	else if(sSubSep != NULL)
		bOk = _dassrc_outAddFlag(pO, pC->sParam, NULL, pC->pParam, 0, NULL, NULL, sSubSep, bHavePVal ? sPVal : sVal);
	else
		bOk = _dassrc_outSet(pO, pC->sParam, bHavePVal ? sPVal : sVal);
	if(!bOk) snprintf(sWhy, uWhy, "Out of memory");
	return bOk;
}

/* A control's value to protocol parameters.  A list goes one item at a
 * time: enumerated items in the node's order, so a selection has one
 * spelling on the wire however it was typed, others as given. */
static bool _DasSrcCtrl_translate(const DasSrcCtrl* pC, const char* sVal, _out_t* pO, char* sWhy, size_t uWhy)
{
	if(!pC->pProp->bMulti) return _DasSrcCtrl_translateOne(pC, sVal, pO, sWhy, uWhy);

	size_t uItems = 0;
	char** aItem = _dassrc_newSplit(sVal, &uItems);
	if(aItem == NULL){ snprintf(sWhy, uWhy, "Out of memory"); return false; }
	bool bOk = true;
	if(pC->uEnum > 0){
		for(size_t e = 0; bOk && (e < pC->uEnum); ++e){
			for(size_t u = 0; u < uItems; ++u){
				if(!_dassrc_strieq(aItem[u], pC->aEnum[e])) continue;
				bOk = _DasSrcCtrl_translateOne(pC, pC->aEnum[e], pO, sWhy, uWhy);
				break;
			}
		}
	}
	else{
		for(size_t u = 0; bOk && (u < uItems); ++u)
			if(aItem[u][0] != '\0') bOk = _DasSrcCtrl_translateOne(pC, aItem[u], pO, sWhy, uWhy);
	}
	free(aItem);
	return bOk;
}

/* The value a requisition gives a property: the last good entry that
 * resolved to it, derived or not.  An empty one takes back what came
 * before, so 'lfdr=' after an example that set options.lfdr leaves the
 * list unset, though the two spellings are two entries.  NULL when the
 * requisition leaves the property alone. */
static const char* _dassrc_reqValueFor(const DasReq* pReq, size_t uProp, bool bList)
{
	const char* sVal = NULL;
	for(size_t u = 0; u < pReq->uEntry; ++u){
		const struct das_req_entry* pE = pReq->aEntry + u;
		if((pE->nProp != (int)uProp)||(pE->verdict != DASREQVERDICT_OK)) continue;
		sVal = _dassrc_noValue(pE->sVal, bList) ? NULL : pE->sVal;
	}
	return sVal;
}

/* As _dassrc_reqValueFor(), with the toggles of an exclusive set giving
 * what was settled for the set and not what was said of each: the format
 * an example switched on is off once another is chosen, though its entry
 * still says true.  A toggle the requisition already has right, or that
 * is where the node has it, is passed through untouched. */
static const char* _DasSrc_valueFor(const DasSrc* pThis, const DasReq* pReq, const _xor_t* aX, size_t uProp)
{
	const DasSrcProp* p = pThis->aProp + uProp;
	const char* sVal = _dassrc_reqValueFor(pReq, uProp, p->bMulti);
	char cState = aX[p->pGroup - pThis->aGroup].cState;
	if((cState == XOR_NONE) || !_DasSrcProp_isEnable(p)) return sVal;

	bool b = false;
	if(_dassrc_parseBool(sVal ? sVal : p->sValue, &b) && (b == (cState == XOR_ON))) return sVal;
	return (cState == XOR_ON) ? "true" : "false";
}

static bool _DasSrcProp_sameAsDefault(const DasSrcProp* p, const char* sVal)
{
	if(p->sValue == NULL) return false;
	if(p->bMulti) return _dassrc_sameItems(sVal, p->sValue);
	if(p->type == DASSRCPROPTYPE_BOOL){
		bool bA = false, bB = false;
		return _dassrc_parseBool(sVal, &bA) && _dassrc_parseBool(p->sValue, &bB) && (bA == bB);
	}
	return _dassrc_strieq(sVal, p->sValue);
}

size_t DasSrc_reqUrl(const DasSrc* pThis, DasReq* pReq, size_t uBase, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	const DasStreamSrc* pS = _DasSrc_stream(pThis);
	if((pS == NULL)||(pS->pProto == NULL)||(pS->pBaseUrls == NULL)){
		daslog_errorf("Source %s has no protocol section with base URLs, it cannot be queried",
		               pThis->sUrl);
		return 0;
	}
	const char* sBase = DasSrc_baseUrlAt(pThis, uBase);
	if(sBase == NULL){
		daslog_errorf("Source %s has %zu base URL(s), there is no number %zu",
		               pThis->sUrl, DasSrc_numBaseUrls(pThis), uBase);
		return 0;
	}

	/* Every reason is logged, not just the first: a blank requisition lists
	   the required keys up front, so the first bad entry is often "min is
	   required" while the user's typo sits further down */
	size_t uBad = 0;
	if(DasSrc_check(pThis, pReq, &uBad) != DAS_OKAY){
		for(size_t u = 0; u < pReq->uEntry; ++u)
			if(pReq->aEntry[u].verdict == DASREQVERDICT_BAD) daslog_errorf("%s", pReq->aEntry[u].sWhy);
		return 0;
	}

	/* Joined values first: which of them does this request send at all?
	   One is sent when any control feeding it is off its default or is
	   required.  Then every control feeding it sends, defaults included.
	   '-hfr Ew' does not mean "sensor Ew, bands as usual" to a server, it
	   means no bands, so the default bands have to travel with it. */
	const DasJdo** aLive = (const DasJdo**)calloc(pThis->uCtrl ? pThis->uCtrl : 1, sizeof(DasJdo*));
	_xor_t* aX = _DasSrc_newXorStates(pThis, pReq);
	if((aLive == NULL)||(aX == NULL)){ free(aLive); free(aX); return 0; }

	/* Two members of an exclusive set on, which only the node can do: a
	   requisition that switches one on switches the rest off.  Either
	   would be a guess. */
	for(size_t g = 0; g < pThis->uGroup; ++g){
		for(size_t h = g + 1; (aX[g].cState == XOR_ON) && (h < pThis->uGroup); ++h){
			if((aX[h].cState != XOR_ON) || (strcmp(pThis->aGroup[g].sXor, pThis->aGroup[h].sXor) != 0)) continue;
			daslog_errorf("Source %s has both %s and %s of the exclusive set '%s' on by default, "
			              "the node is broken.  Switch one of them on to say which is meant.",
			              pThis->sUrl, pThis->aGroup[g].sName, pThis->aGroup[h].sName, pThis->aGroup[g].sXor);
			free(aLive); free(aX);
			return 0;
		}
	}

	size_t uLive = 0;
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		const DasSrcCtrl* pC = p->pCtrl;
		const DasJdo* pT = pC ? _DasSrcCtrl_joinTarget(pC) : NULL;
		if((pT == NULL) || _DasSrcProp_xorIdle(pThis, aX, p)) continue;
		const char* sVal = _DasSrc_valueFor(pThis, pReq, aX, u);
		bool bSends = (sVal != NULL) ? (pC->bRequired || !_DasSrcProp_sameAsDefault(p, sVal)) :
		                               (pC->bRequired && (p->sValue != NULL));
		for(size_t v = 0; bSends && (v < uLive); ++v) if(aLive[v] == pT) bSends = false;
		if(bSends) aLive[uLive++] = pT;
	}

	_out_t out = {NULL, 0, 0};
	char sWhy[1024];
	for(size_t u = 0; u < pThis->uProp; ++u){
		const DasSrcProp* p = pThis->aProp + u;
		const DasSrcCtrl* pC = p->pCtrl;
		if(pC == NULL) continue;

		/* Nothing of a format not chosen is sent, typed or default.
		   (Typed settings were refused by the check above.) */
		if(_DasSrcProp_xorIdle(pThis, aX, p)) continue;

		bool bLive = false;
		const DasJdo* pT = _DasSrcCtrl_joinTarget(pC);
		for(size_t v = 0; (pT != NULL) && (v < uLive); ++v) if(aLive[v] == pT) bLive = true;

		const char* sVal = _DasSrc_valueFor(pThis, pReq, aX, u);
		bool bDefault = false;
		if(sVal == NULL){
			if((!pC->bRequired && !bLive) || (p->sValue == NULL)) continue;
			sVal = p->sValue;
			bDefault = true;
		}
		/* A required default fills a hole, it never replaces what another
		   control already put in a shared parameter.  Part of a joined
		   value replaces nothing, it adds. */
		if(bDefault && !bLive && _dassrc_outFind(&out, pC->sParam)) continue;

		/* A value equal to the default carries no information unless the
		   server insists on hearing it, or it is part of a joined value
		   that is going out anyway */
		if(!bDefault && !pC->bRequired && !bLive && _DasSrcProp_sameAsDefault(p, sVal)) continue;

		/* A switch adds its element to a joined value in one state only,
		   on unless its set block says otherwise.  In the other state it
		   has nothing to add, which is not the same as having no element
		   to give, the node error _DasSrcCtrl_translateOne() reports. */
		if(bLive && (p->type == DASSRCPROPTYPE_BOOL)){
			bool bState = false;
			_dassrc_parseBool(sVal, &bState);
			const DasJdo* pTrig = _dassrc_jget(pC->pSet, "value");
			if(bState != (_dassrc_jisbool(pTrig) ? _dassrc_jtrue(pTrig) : true)) continue;
		}

		if(!_DasSrcCtrl_translate(pC, sVal, &out, sWhy, sizeof(sWhy))){
			daslog_errorf("%s", sWhy);
			_dassrc_outClear(&out);
			free(aLive); free(aX);
			return 0;
		}
	}
	free(aLive);
	free(aX);

	for(size_t u = 0; u < pReq->uRaw; ++u){
		if(!_dassrc_outSet(&out, pReq->aRaw[u].sName, pReq->aRaw[u].sVal)){ _dassrc_outClear(&out); return 0; }
	}

	/* Protocol parameters the node marks required, whatever supplies them */
	for(const DasJsonDictEl* pEl = DasJdo_dictFirst(pS->pParams); pEl; pEl = pEl->next){
		if(!_dassrc_jtrue(_dassrc_jget(pEl->value, "required"))) continue;
		const char* sName = pEl->name->string;
		if(_dassrc_outFind(&out, sName) != NULL) continue;

		size_t n = snprintf(sWhy, sizeof(sWhy), "Required parameter %s missing, set it with", sName);
		int nKeys = 0;
		for(size_t i = 0; i < pThis->uProp; ++i){
			const DasSrcCtrl* pC = pThis->aProp[i].pCtrl;
			if(pC && (strcmp(pC->sParam, sName) == 0)){
				n = _dassrc_catf(sWhy, sizeof(sWhy), n, "%s %s=VALUE", nKeys ? " or" : "", pThis->aProp[i].sKey);
				++nKeys;
			}
		}
		if(nKeys == 0) _dassrc_catf(sWhy, sizeof(sWhy), n, " a raw parameter, no interface key sets it");
		daslog_errorf("%s", sWhy);
		_dassrc_outClear(&out);
		return 0;
	}

	size_t u = _dassrc_catf(sBuf, uLen, 0, "%s", sBase);
	size_t uNeed = strlen(sBase);
	const char* sJoin = strchr(sBase, '?') ? "&" : "?";
	for(size_t i = 0; i < out.u; ++i){
		/* one pair, or one per piece for a parameter that repeats */
		size_t uPairs = out.a[i].bRepeat ? out.a[i].uPiece : 1;
		for(size_t v = 0; v < uPairs; ++v){
			char* sVal = out.a[i].bRepeat ? _dassrc_strdupz(out.a[i].aPiece[v].sVal) :
			                                _dassrc_outJoin(out.a + i);
			if(sVal == NULL){ _dassrc_outClear(&out); return 0; }
			size_t uEnc = dasurl_encode(sVal, NULL, 0) + 1;
			char* sEnc = (char*)malloc(uEnc);
			if(sEnc == NULL){ free(sVal); _dassrc_outClear(&out); return 0; }
			dasurl_encode(sVal, sEnc, uEnc);
			free(sVal);
			u = _dassrc_catf(sBuf, uLen, u, "%s%s=%s", sJoin, out.a[i].sName, sEnc);
			uNeed += strlen(sJoin) + strlen(out.a[i].sName) + 1 + strlen(sEnc);
			free(sEnc);
			sJoin = "&";
		}
	}
	_dassrc_outClear(&out);
	return uNeed;
}

/* ************************************************************************* */
/* ************************************************************************* */
/* The sources under a container */

size_t DasSrc_loadable(const DasNode* pNode, const char** aNames, size_t uMax)
{
	if((pNode == NULL) || !DasNode_isCatalog(pNode)) return 0;
	size_t uKids = DasNode_numSubs(pNode);
	size_t uFound = 0;
	for(size_t u = 0; u < uKids; ++u){
		if(DasNode_subTypeAt(pNode, u) != DASNODETYPE_STREAM_SRC) continue;
		if(uFound < uMax) aNames[uFound] = DasNode_subNameAt(pNode, u);
		++uFound;
	}
	return uFound;
}
