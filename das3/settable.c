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

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "time.h"
#include "sett_priv.h"

/* Section names as the current catalogs spell them.  Keys are built from
 * these; input accepts the singular forms too, see _das_src_secByName(). */
static const char* g_sSecName[das_src_sec_size] = {"coords", "data", "formats", "options"};

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

bool _das_json_scalar(const DasJdo* p, char* sBuf, size_t uLen)
{
	if((p == NULL)||(uLen == 0)) return false;
	switch(p->type){
	case das_json_type_str:
		_strcpyz(sBuf, ((const das_json_str*)p->value)->string, uLen);
		return true;
	case das_json_type_num:{
		const das_json_num* pNum = (const das_json_num*)p->value;
		size_t u = (pNum->number_size < uLen - 1) ? pNum->number_size : uLen - 1;
		memcpy(sBuf, pNum->number, u);
		sBuf[u] = '\0';
		return true;
	}
	case das_json_type_true:  _strcpyz(sBuf, "true", uLen);  return true;
	case das_json_type_false: _strcpyz(sBuf, "false", uLen); return true;
	default: return false;
	}
}

/* A heap copy of a scalar, NULL for anything that is not one.  Defaults,
 * enum entries and range ends are short in practice; the longest seen is a
 * few dozen bytes, so anything past this is cut rather than carried. */
#define SCALAR_SZ 1024
static char* _scalarDup(const DasJdo* p)
{
	char sBuf[SCALAR_SZ];
	if(!_das_json_scalar(p, sBuf, sizeof(sBuf))) return NULL;
	return das_strdup(sBuf);
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
/* Parsing */

/* Fold a type word from a node to the enum.  NULL and unknown words are
 * text, the caller decides whether that is final. */
static bool _typeByName(const char* s, das_settable_type_e* pType)
{
	if(s == NULL) return false;
	if(_strieq(s,"boolean")||_strieq(s,"bool")){ *pType = das_settable_bool; return true; }
	if(_strieq(s,"integer")||_strieq(s,"int")){ *pType = das_settable_int; return true; }
	if(_strieq(s,"real")||_strieq(s,"double")||_strieq(s,"float")){
		*pType = das_settable_real; return true;
	}
	if(_strieq(s,"isotime")||_strieq(s,"time")||_strieq(s,"datetime")){
		*pType = das_settable_time; return true;
	}
	if(_strieq(s,"enum")){ *pType = das_settable_enum; return true; }
	if(_strieq(s,"string")||_strieq(s,"text")){ *pType = das_settable_text; return true; }
	return false;
}

/* Preference order: a set enum list, the property's own type, a boolean
 * default, a flag toggle, the protocol parameter's type, then text.  The
 * enum list goes first because a property declared "string" with a list of
 * choices is still a list of choices to anyone presenting it.  A flag set
 * parameter type is a server side packing detail and says nothing about
 * the single toggle that feeds it. */
static void _resolveType(DasSettable* p)
{
	p->sTypeName = _jstr(p->pProp, "type");
	if(p->sTypeName == NULL) p->sTypeName = "";

	if(p->uEnum > 0){ p->type = das_settable_enum; return; }
	if(_typeByName(p->sTypeName, &(p->type))){
		if(p->type != das_settable_enum) return;
	}
	if(_jisbool(_jget(p->pProp, "value")) || (_jstr(p->pSet, "flag") != NULL)){
		p->type = das_settable_bool; return;
	}
	const char* sParamType = _jstr(p->pParam, "type");
	if(_typeByName(sParamType, &(p->type)) && (p->type != das_settable_enum)){
		if(p->sTypeName[0] == '\0') p->sTypeName = sParamType;
		return;
	}
	p->type = das_settable_text;
}

/* Enum entries are usually dicts carrying value plus per item pval or flag
 * overrides, but a bare scalar list is accepted too. */
static bool _fillEnum(DasSettable* p)
{
	const DasJdo* pEnum = _jget(p->pSet, "enum");
	if(!_jisary(pEnum)) return true;

	size_t uMax = ((const das_json_ary*)pEnum->value)->length;
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

static void _fillRange(DasSettable* p)
{
	const DasJdo* pRange = _jget(p->pSet, "range");
	if(!_jisary(pRange)) pRange = _jget(p->pParam, "range");
	if(!_jisary(pRange) && (p->sec == das_src_sec_coord) &&
	   (_das_src_propEq(p->sProp, "min") || _das_src_propEq(p->sProp, "max")))
		pRange = _jget(p->pGroup, "validRange");
	if(!_jisary(pRange)) return;

	const das_json_ary_el* pEl = DasJdo_aryFirst(pRange);
	if(pEl == NULL) return;
	p->sLo = _scalarDup(pEl->value);
	if(pEl->next) p->sHi = _scalarDup(pEl->next->value);
}

/* One property.  With aOut NULL only the count is wanted.  Returns false
 * on allocation failure, the only way filling can fail. */
static bool _addProp(
	das_src_sec_e sec, const char* sGroup, const DasJdo* pGroup,
	const char* sProp, const DasJdo* pProp, const DasJdo* pParams,
	DasSettable* aOut, size_t* puCount
){
	if(!_jisdict(pProp)) return true;
	const DasJdo* pSet = _jget(pProp, "set");
	if(!_jisdict(pSet)) return true;    /* documentation only, not settable */

	const char* sParam = _jstr(pSet, "param");
	if(sParam == NULL){
		if(aOut != NULL)
			daslog_warn_v("Ignoring %s.%s%s%s, its set block names no parameter",
			              g_sSecName[sec], sGroup, sGroup[0] ? "." : "", sProp);
		return true;
	}

	if(aOut == NULL){ *puCount += 1; return true; }

	DasSettable* p = aOut + *puCount;
	memset(p, 0, sizeof(DasSettable));
	p->sec = sec;
	p->sGroup = sGroup;
	p->sProp = sProp;
	p->pGroup = pGroup;
	p->pProp = pProp;
	p->pSet = pSet;
	p->sParam = sParam;
	p->pParam = _jget(pParams, sParam);
	p->bRequired = _jtrue(_jget(pSet, "required")) || _jtrue(_jget(p->pParam, "required"));

	size_t uKey = strlen(g_sSecName[sec]) + strlen(sGroup) + strlen(sProp) + 3;
	p->sKey = (char*)malloc(uKey);
	if(p->sKey == NULL) return false;
	if(sGroup[0] == '\0')
		snprintf(p->sKey, uKey, "%s.%s", g_sSecName[sec], sProp);
	else
		snprintf(p->sKey, uKey, "%s.%s.%s", g_sSecName[sec], sGroup, sProp);

	p->sDefVal = _scalarDup(_jget(pProp, "value"));
	if(!_fillEnum(p)) return false;
	_fillRange(p);
	_resolveType(p);

	*puCount += 1;
	return true;
}

/* A group is a coordinate, a data variable, a format, or the whole options
 * block.  Current groups keep properties under "props", 2018 groups hold
 * them directly beside label/title/units.  Anything without a 'set' is
 * skipped either way, so iterating the group itself is safe for the old
 * layout. */
static bool _addGroup(
	das_src_sec_e sec, const char* sGroup, const DasJdo* pGroup,
	const DasJdo* pParams, DasSettable* aOut, size_t* puCount
){
	if(!_jisdict(pGroup)) return true;
	const DasJdo* pProps = _jget(pGroup, "props");
	const DasJdo* pIter = _jisdict(pProps) ? pProps : pGroup;
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pIter); pEl; pEl = pEl->next){
		if(!_addProp(sec, sGroup, pGroup, pEl->name->string, pEl->value, pParams, aOut, puCount))
			return false;
	}
	return true;
}

static bool _addSection(
	das_src_sec_e sec, const DasJdo* pSec, const DasJdo* pParams,
	DasSettable* aOut, size_t* puCount
){
	if(!_jisdict(pSec)) return true;
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next){
		if(!_addGroup(sec, pEl->name->string, pEl->value, pParams, aOut, puCount))
			return false;
	}
	return true;
}

DasSettable* _DasSettable_parse(const DasJdo* pIface, const DasJdo* pParams, size_t* puCount)
{
	*puCount = 0;
	if(!_jisdict(pIface)) pIface = NULL;

	/* Two passes over the same walk: count, allocate exactly that many,
	   fill.  Options are a group with no enclosing section, so they go
	   through _addGroup directly and their pGroup is NULL. */
	size_t uCount = 0;
	const DasJdo* pSec = _jget(pIface, "coords");
	if(pSec == NULL) pSec = _jget(pIface, "coordinates");
	const DasJdo* pOpts = _jget(pIface, "options");

	_addSection(das_src_sec_coord,  pSec, pParams, NULL, &uCount);
	_addSection(das_src_sec_data,   _jget(pIface, "data"), pParams, NULL, &uCount);
	_addSection(das_src_sec_format, _jget(pIface, "formats"), pParams, NULL, &uCount);
	_addGroup(das_src_sec_option, "", pOpts, pParams, NULL, &uCount);

	/* An empty list is still a list: one byte keeps malloc from handing
	   back NULL on some platforms and lets callers test the count alone. */
	DasSettable* aOut = (DasSettable*)calloc(uCount ? uCount : 1, sizeof(DasSettable));
	if(aOut == NULL){
		daslog_error_v("Couldn't allocate %zu source settings", uCount);
		return NULL;
	}

	size_t uFill = 0;
	bool bOk =
		_addSection(das_src_sec_coord,  pSec, pParams, aOut, &uFill) &&
		_addSection(das_src_sec_data,   _jget(pIface, "data"), pParams, aOut, &uFill) &&
		_addSection(das_src_sec_format, _jget(pIface, "formats"), pParams, aOut, &uFill) &&
		_addGroup(das_src_sec_option, "", pOpts, pParams, aOut, &uFill);

	if(!bOk){
		daslog_error_v("Couldn't allocate the strings of source setting %zu", uFill);
		_DasSettable_freeList(aOut, uFill + 1);
		return NULL;
	}
	*puCount = uFill;
	return aOut;
}

void _DasSettable_freeList(DasSettable* aList, size_t uCount)
{
	if(aList == NULL) return;
	for(size_t u = 0; u < uCount; ++u){
		DasSettable* p = aList + u;
		free(p->sKey);
		free(p->sDefVal);
		for(size_t v = 0; v < p->uEnum; ++v) free(p->aEnum[v]);
		free(p->aEnum);
		free(p->sLo);
		free(p->sHi);
	}
	free(aList);
}

/* ************************************************************************* */
/* Accessors */

das_src_sec_e DasSettable_section(const DasSettable* p){ return p->sec; }
const char* DasSettable_group(const DasSettable* p){ return p->sGroup; }
const char* DasSettable_prop(const DasSettable* p){ return p->sProp; }
const char* DasSettable_key(const DasSettable* p){ return p->sKey; }
const char* DasSettable_param(const DasSettable* p){ return p->sParam; }
bool DasSettable_required(const DasSettable* p){ return p->bRequired; }
const char* DasSettable_defVal(const DasSettable* p){ return p->sDefVal; }
das_settable_type_e DasSettable_type(const DasSettable* p){ return p->type; }
const char* DasSettable_typeName(const DasSettable* p){ return p->sTypeName; }
size_t DasSettable_enumCount(const DasSettable* p){ return p->uEnum; }

const char* DasSettable_enum(const DasSettable* p, size_t uIdx)
{
	return (uIdx < p->uEnum) ? p->aEnum[uIdx] : NULL;
}

bool DasSettable_range(const DasSettable* p, const char** psLo, const char** psHi)
{
	if((p->sLo == NULL)&&(p->sHi == NULL)) return false;
	if(psLo) *psLo = p->sLo ? p->sLo : "";
	if(psHi) *psHi = p->sHi ? p->sHi : "";
	return true;
}

/* Units for display.  Current nodes put a units string on the property;
 * both layouts may carry a units dict with a "value" at the group level,
 * under props in the current layout and directly in the old one. */
const char* DasSettable_units(const DasSettable* p)
{
	const DasJdo* pU = _jget(p->pProp, "units");
	if(pU != NULL){
		if(pU->type == das_json_type_str) return DasJdo_string(pU);
		if(_jstr(pU, "value")) return _jstr(pU, "value");
	}
	if(p->sec == das_src_sec_coord){
		const char* s = _jstr(p->pGroup, "props/units/value");
		if(s == NULL) s = _jstr(p->pGroup, "units/value");
		if(s != NULL) return s;
	}
	if(p->type == das_settable_time) return "UTC";
	return "";
}

/* What to call a group in composed text: its label, name or title, falling
 * back to the dict key */
static const char* _groupLabel(const DasSettable* p)
{
	const char* s = _jstr(p->pGroup, "label");
	if(s == NULL) s = _jstr(p->pGroup, "name");
	if(s == NULL) s = _jstr(p->pGroup, "title");
	return s ? s : p->sGroup;
}

const char* DasSettable_title(const DasSettable* p, char* sBuf, size_t uLen)
{
	const char* s = _jstr(p->pProp, "title");
	if(s == NULL) s = _jstr(p->pProp, "summary");
	if(s == NULL) s = _jstr(p->pProp, "label");
	if(s == NULL) s = _jstr(p->pProp, "name");
	if(s != NULL) return s;
	if(p->sec == das_src_sec_option) return "";

	const char* sProp = _das_src_propName(p->sProp);
	if(strcmp(sProp, "enabled") == 0){
		if(p->sec == das_src_sec_format){
			snprintf(sBuf, uLen, "Output as %s", _groupLabel(p));
		}
		else{
			/* A variable's title is usually the physical quantity, which
			   reads better after "Output" than its short label does */
			const char* sTitle = _jstr(p->pGroup, "title");
			snprintf(sBuf, uLen, "Output %s", sTitle ? sTitle : _groupLabel(p));
		}
		return sBuf;
	}
	if(p->sec == das_src_sec_coord){
		const char* sFmt = NULL;
		if(strcmp(sProp, "min") == 0)        sFmt = "Minimum %s value to output";
		else if(strcmp(sProp, "max") == 0)   sFmt = "Maximum %s value to output";
		else if(strcmp(sProp, "res") == 0)   sFmt = "Average over %s bins of this width";
		else if(strcmp(sProp, "inter") == 0) sFmt = "Step between output %s values";
		if(sFmt != NULL){
			snprintf(sBuf, uLen, sFmt, _groupLabel(p));
			return sBuf;
		}
	}
	snprintf(sBuf, uLen, "%s %s", _groupLabel(p), p->sProp);
	return sBuf;
}

/* ************************************************************************* */
/* Validation.  Failures are logged and returned, not raised through
 * das_error(), because these are user typed values and the default error
 * disposition would exit the application over a typo. */

DasErrCode DasSettable_check(const DasSettable* p, const char* sVal)
{
	if(sVal == NULL) sVal = "";
	char* pEnd = NULL;

	switch(p->type){
	case das_settable_bool:{
		bool b;
		if(_parseBool(sVal, &b)) return DAS_OKAY;
		daslog_error_v("'%s' is not a boolean value for %s, use true or false", sVal, p->sKey);
		return DASERR_SRC;
	}
	case das_settable_enum:{
		for(size_t u = 0; u < p->uEnum; ++u)
			if(_strieq(p->aEnum[u], sVal)) return DAS_OKAY;

		char sChoices[512] = {'\0'};
		size_t u = 0;
		for(size_t v = 0; (v < p->uEnum) && (u < sizeof(sChoices)); ++v)
			u += snprintf(sChoices + u, sizeof(sChoices) - u, "%s%s", v ? ", " : "", p->aEnum[v]);
		daslog_error_v("'%s' is not an allowed value for %s, choose one of: %s",
		               sVal, p->sKey, sChoices);
		return DASERR_SRC;
	}
	case das_settable_int:
		strtol(sVal, &pEnd, 10);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		daslog_error_v("'%s' is not an integer value for %s", sVal, p->sKey);
		return DASERR_SRC;
	case das_settable_real:
		strtod(sVal, &pEnd);
		if((pEnd != sVal) && (*pEnd == '\0')) return DAS_OKAY;
		daslog_error_v("'%s' is not a real value for %s", sVal, p->sKey);
		return DASERR_SRC;
	case das_settable_time:{
		/* "now" is how nodes spell an open ended range and servers take
		   it in requests too */
		das_time dt;
		if(_strieq(sVal, "now") || dt_parsetime(&dt, sVal)) return DAS_OKAY;
		daslog_error_v("'%s' is not a time value for %s, use ISO 8601 such as "
		               "2026-01-15T12:00", sVal, p->sKey);
		return DASERR_SRC;
	}
	default:
		return DAS_OKAY;
	}
}
