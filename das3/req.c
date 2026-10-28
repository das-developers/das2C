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

/* A requisition is strings all the way down, see req.h for why.  Nothing
 * in this file knows what a key means; that is source.c's job. */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "req_priv.h"

/* ************************************************************************* */
/* Helpers */

static bool _dasreq_strieq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return false;
	while(*a && *b){
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		++a; ++b;
	}
	return (*a == '\0') && (*b == '\0');
}

/* das_strdup() returns NULL for NULL and for "", and here "" is a value:
   an empty stamp, an unfilled entry.  Every string in a requisition goes
   through this so accessors can hand them straight back. */
static char* _dasreq_dupz(const char* s)
{
	if((s == NULL)||(s[0] == '\0')) return (char*)calloc(1, 1);
	return das_strdup(s);
}

static bool _dasreq_setStr(char** ps, const char* s)
{
	char* sNew = _dasreq_dupz(s);
	if(sNew == NULL) return false;
	free(*ps);
	*ps = sNew;
	return true;
}

bool _das_json_scalar(const DasJdo* p, char* sBuf, size_t uLen)
{
	if((p == NULL)||(uLen == 0)) return false;
	switch(p->type){
	case das_json_type_str:{
		const char* s = ((const das_json_str*)p->value)->string;
		strncpy(sBuf, s, uLen - 1);
		sBuf[uLen - 1] = '\0';
		return true;
	}
	case das_json_type_num:{
		const das_json_num* pNum = (const das_json_num*)p->value;
		size_t u = (pNum->number_size < uLen - 1) ? pNum->number_size : uLen - 1;
		memcpy(sBuf, pNum->number, u);
		sBuf[u] = '\0';
		return true;
	}
	case das_json_type_true:  strncpy(sBuf, "true", uLen);  sBuf[uLen-1] = '\0'; return true;
	case das_json_type_false: strncpy(sBuf, "false", uLen); sBuf[uLen-1] = '\0'; return true;
	default: return false;
	}
}

/* ************************************************************************* */
/* Life cycle */

DasReq* new_DasReq(void)
{
	DasReq* pThis = (DasReq*)calloc(1, sizeof(DasReq));
	if(pThis == NULL) return NULL;
	pThis->sPath = _dasreq_dupz(NULL);
	pThis->sUrl = _dasreq_dupz(NULL);
	pThis->sVersion = _dasreq_dupz(NULL);
	pThis->sLabel = _dasreq_dupz(NULL);
	if(!pThis->sPath || !pThis->sUrl || !pThis->sVersion || !pThis->sLabel){
		del_DasReq(pThis);
		return NULL;
	}
	return pThis;
}

void del_DasReq(DasReq* pThis)
{
	if(pThis == NULL) return;
	DasReq_clear(pThis);
	free(pThis->aEntry);
	free(pThis->aRaw);
	free(pThis->sPath);
	free(pThis->sUrl);
	free(pThis->sVersion);
	free(pThis->sLabel);
	free(pThis);
}

DasReq* copy_DasReq(const DasReq* pThis)
{
	DasReq* pNew = new_DasReq();
	if(pNew == NULL) return NULL;
	DasReq_setSrc(pNew, pThis->sPath, pThis->sUrl, pThis->sVersion);
	DasReq_setLabel(pNew, pThis->sLabel);
	for(size_t u = 0; u < pThis->uEntry; ++u){
		const struct das_req_entry* pE = pThis->aEntry + u;
		int n = _DasReq_add(pNew, pE->sKey, pE->sVal, pE->bDerived);
		if(n < 0){ del_DasReq(pNew); return NULL; }
		_DasReq_setVerdict(pNew, (size_t)n, pE->verdict, pE->sWhy);
	}
	for(size_t u = 0; u < pThis->uRaw; ++u){
		if(DasReq_setRaw(pNew, pThis->aRaw[u].sName, pThis->aRaw[u].sVal) != DAS_OKAY){
			del_DasReq(pNew); return NULL;
		}
	}
	return pNew;
}

/* ************************************************************************* */
/* Stamp and label */

void DasReq_setSrc(DasReq* pThis, const char* sPath, const char* sUrl, const char* sVersion)
{
	_dasreq_setStr(&pThis->sPath, sPath);
	_dasreq_setStr(&pThis->sUrl, sUrl);
	_dasreq_setStr(&pThis->sVersion, sVersion);
}

const char* DasReq_srcPath(const DasReq* pThis){ return pThis->sPath; }
const char* DasReq_srcUrl(const DasReq* pThis){ return pThis->sUrl; }
const char* DasReq_srcVersion(const DasReq* pThis){ return pThis->sVersion; }
const char* DasReq_label(const DasReq* pThis){ return pThis->sLabel; }
void DasReq_setLabel(DasReq* pThis, const char* sLabel){ _dasreq_setStr(&pThis->sLabel, sLabel); }

/* ************************************************************************* */
/* Entries */

static void _dasreq_freeEntry(struct das_req_entry* pE)
{
	free(pE->sKey);
	free(pE->sVal);
	free(pE->sWhy);
	memset(pE, 0, sizeof(struct das_req_entry));
}

int _DasReq_add(DasReq* pThis, const char* sKey, const char* sVal, bool bDerived)
{
	if(pThis->uEntry == pThis->uEntryCap){
		size_t uCap = pThis->uEntryCap ? pThis->uEntryCap * 2 : 8;
		struct das_req_entry* a = (struct das_req_entry*)realloc(
			pThis->aEntry, uCap * sizeof(struct das_req_entry)
		);
		if(a == NULL) return -1;
		pThis->aEntry = a;
		pThis->uEntryCap = uCap;
	}
	struct das_req_entry* pE = pThis->aEntry + pThis->uEntry;
	memset(pE, 0, sizeof(struct das_req_entry));
	pE->sKey = _dasreq_dupz(sKey);
	pE->sVal = _dasreq_dupz(sVal);
	if((pE->sKey == NULL)||(pE->sVal == NULL)){ _dasreq_freeEntry(pE); return -1; }
	pE->bDerived = bDerived;
	pE->nProp = -1;
	pThis->uEntry += 1;
	return (int)(pThis->uEntry - 1);
}

void _DasReq_dropDerived(DasReq* pThis)
{
	size_t uKeep = 0;
	for(size_t u = 0; u < pThis->uEntry; ++u){
		if(pThis->aEntry[u].bDerived){ _dasreq_freeEntry(pThis->aEntry + u); continue; }
		if(uKeep != u) pThis->aEntry[uKeep] = pThis->aEntry[u];
		++uKeep;
	}
	pThis->uEntry = uKeep;
}

void _DasReq_setVerdict(DasReq* pThis, size_t uIdx, das_req_verdict_e verdict, const char* sWhy)
{
	if(uIdx >= pThis->uEntry) return;
	struct das_req_entry* pE = pThis->aEntry + uIdx;
	pE->verdict = verdict;
	free(pE->sWhy);
	pE->sWhy = (verdict == das_req_bad) ? _dasreq_dupz(sWhy) : NULL;
}

void _DasReq_clearVerdicts(DasReq* pThis)
{
	for(size_t u = 0; u < pThis->uEntry; ++u){
		pThis->aEntry[u].verdict = das_req_unchecked;
		pThis->aEntry[u].nProp = -1;
		free(pThis->aEntry[u].sWhy);
		pThis->aEntry[u].sWhy = NULL;
	}
}

int DasReq_find(const DasReq* pThis, const char* sKey)
{
	for(size_t u = 0; u < pThis->uEntry; ++u)
		if(!pThis->aEntry[u].bDerived && _dasreq_strieq(pThis->aEntry[u].sKey, sKey)) return (int)u;
	return -1;
}

DasErrCode DasReq_set(DasReq* pThis, const char* sKey, const char* sVal)
{
	if((sKey == NULL)||(sKey[0] == '\0')){
		daslog_error("Requisition keys may not be empty");
		return DASERR_SRC;
	}
	/* Any change makes the last check stale for the derived entries, they
	   are the check's output */
	_DasReq_dropDerived(pThis);

	int n = DasReq_find(pThis, sKey);
	if(n >= 0){
		struct das_req_entry* pE = pThis->aEntry + n;
		char* sNew = _dasreq_dupz(sVal);
		if(sNew == NULL) return DASERR_SRC;
		free(pE->sVal);
		pE->sVal = sNew;
		pE->verdict = das_req_unchecked;
		free(pE->sWhy);
		pE->sWhy = NULL;
		return DAS_OKAY;
	}
	return (_DasReq_add(pThis, sKey, sVal, false) < 0) ? DASERR_SRC : DAS_OKAY;
}

const char* DasReq_value(const DasReq* pThis, const char* sKey)
{
	int n = DasReq_find(pThis, sKey);
	return (n < 0) ? NULL : pThis->aEntry[n].sVal;
}

bool DasReq_remove(DasReq* pThis, const char* sKey)
{
	int n = DasReq_find(pThis, sKey);
	if(n < 0) return false;
	_dasreq_freeEntry(pThis->aEntry + n);
	memmove(pThis->aEntry + n, pThis->aEntry + n + 1,
	        (pThis->uEntry - n - 1) * sizeof(struct das_req_entry));
	pThis->uEntry -= 1;
	_DasReq_dropDerived(pThis);
	return true;
}

void DasReq_clear(DasReq* pThis)
{
	for(size_t u = 0; u < pThis->uEntry; ++u) _dasreq_freeEntry(pThis->aEntry + u);
	pThis->uEntry = 0;
	for(size_t u = 0; u < pThis->uRaw; ++u){
		free(pThis->aRaw[u].sName);
		free(pThis->aRaw[u].sVal);
	}
	pThis->uRaw = 0;
}

size_t DasReq_count(const DasReq* pThis){ return pThis->uEntry; }

const char* DasReq_key(const DasReq* pThis, size_t u)
{
	return (u < pThis->uEntry) ? pThis->aEntry[u].sKey : NULL;
}

const char* DasReq_val(const DasReq* pThis, size_t u)
{
	return (u < pThis->uEntry) ? pThis->aEntry[u].sVal : NULL;
}

das_req_verdict_e DasReq_verdict(const DasReq* pThis, size_t u)
{
	return (u < pThis->uEntry) ? pThis->aEntry[u].verdict : das_req_unchecked;
}

const char* DasReq_why(const DasReq* pThis, size_t u)
{
	if((u >= pThis->uEntry)||(pThis->aEntry[u].sWhy == NULL)) return "";
	return pThis->aEntry[u].sWhy;
}

bool DasReq_derived(const DasReq* pThis, size_t u)
{
	return (u < pThis->uEntry) ? pThis->aEntry[u].bDerived : false;
}

/* ************************************************************************* */
/* Raw parameters.  Names are protocol names and compare exactly. */

DasErrCode DasReq_setRaw(DasReq* pThis, const char* sName, const char* sVal)
{
	if((sName == NULL)||(sName[0] == '\0')){
		daslog_error("Protocol parameter names may not be empty");
		return DASERR_SRC;
	}
	for(size_t u = 0; u < pThis->uRaw; ++u){
		if(strcmp(pThis->aRaw[u].sName, sName) == 0)
			return _dasreq_setStr(&pThis->aRaw[u].sVal, sVal) ? DAS_OKAY : DASERR_SRC;
	}
	if(pThis->uRaw == pThis->uRawCap){
		size_t uCap = pThis->uRawCap ? pThis->uRawCap * 2 : 4;
		struct das_req_raw* a = (struct das_req_raw*)realloc(
			pThis->aRaw, uCap * sizeof(struct das_req_raw)
		);
		if(a == NULL) return DASERR_SRC;
		pThis->aRaw = a;
		pThis->uRawCap = uCap;
	}
	struct das_req_raw* pR = pThis->aRaw + pThis->uRaw;
	pR->sName = _dasreq_dupz(sName);
	pR->sVal = _dasreq_dupz(sVal);
	if((pR->sName == NULL)||(pR->sVal == NULL)){
		free(pR->sName); free(pR->sVal);
		return DASERR_SRC;
	}
	pThis->uRaw += 1;
	return DAS_OKAY;
}

size_t DasReq_rawCount(const DasReq* pThis){ return pThis->uRaw; }

const char* DasReq_raw(const DasReq* pThis, size_t u, const char** psVal)
{
	if(u >= pThis->uRaw) return NULL;
	if(psVal) *psVal = pThis->aRaw[u].sVal;
	return pThis->aRaw[u].sName;
}

/* ************************************************************************* */
/* Saving.  A hand rolled writer because the document is three dicts of
 * strings and das_json has no builder. */

typedef struct { char* s; size_t u; size_t uCap; bool bFail; } _out_t;

static void _dasreq_put(_out_t* pO, const char* s)
{
	size_t n = strlen(s);
	if(pO->u + n + 1 > pO->uCap){
		size_t uCap = (pO->uCap ? pO->uCap * 2 : 256);
		while(uCap < pO->u + n + 1) uCap *= 2;
		char* sNew = (char*)realloc(pO->s, uCap);
		if(sNew == NULL){ pO->bFail = true; return; }
		pO->s = sNew;
		pO->uCap = uCap;
	}
	memcpy(pO->s + pO->u, s, n + 1);
	pO->u += n;
}

static void _dasreq_putStr(_out_t* pO, const char* s)
{
	_dasreq_put(pO, "\"");
	char sEsc[8];
	for(const unsigned char* p = (const unsigned char*)s; *p; ++p){
		switch(*p){
		case '"':  _dasreq_put(pO, "\\\""); break;
		case '\\': _dasreq_put(pO, "\\\\"); break;
		case '\n': _dasreq_put(pO, "\\n");  break;
		case '\r': _dasreq_put(pO, "\\r");  break;
		case '\t': _dasreq_put(pO, "\\t");  break;
		default:
			if(*p < 0x20){ snprintf(sEsc, sizeof(sEsc), "\\u%04x", *p); _dasreq_put(pO, sEsc); }
			else{ sEsc[0] = (char)*p; sEsc[1] = '\0'; _dasreq_put(pO, sEsc); }
		}
	}
	_dasreq_put(pO, "\"");
}

static void _dasreq_putPair(_out_t* pO, const char* sKey, const char* sVal, bool bFirst)
{
	if(!bFirst) _dasreq_put(pO, ",\n");
	_dasreq_put(pO, "    ");
	_dasreq_putStr(pO, sKey);
	_dasreq_put(pO, ": ");
	_dasreq_putStr(pO, sVal);
}

char* DasReq_text(const DasReq* pThis)
{
	_out_t o = {NULL, 0, 0, false};
	_dasreq_put(&o, "{\n  \"source\": {\n");
	_dasreq_putPair(&o, "path", pThis->sPath, true);
	_dasreq_putPair(&o, "url", pThis->sUrl, false);
	_dasreq_putPair(&o, "version", pThis->sVersion, false);
	_dasreq_put(&o, "\n  },\n  \"label\": ");
	_dasreq_putStr(&o, pThis->sLabel);
	_dasreq_put(&o, ",\n  \"settings\": {\n");
	bool bFirst = true;
	for(size_t u = 0; u < pThis->uEntry; ++u){
		if(pThis->aEntry[u].bDerived) continue;
		_dasreq_putPair(&o, pThis->aEntry[u].sKey, pThis->aEntry[u].sVal, bFirst);
		bFirst = false;
	}
	_dasreq_put(&o, "\n  },\n  \"params\": {\n");
	for(size_t u = 0; u < pThis->uRaw; ++u)
		_dasreq_putPair(&o, pThis->aRaw[u].sName, pThis->aRaw[u].sVal, u == 0);
	_dasreq_put(&o, "\n  }\n}\n");
	if(o.bFail){ free(o.s); return NULL; }
	return o.s;
}

DasErrCode DasReq_write(const DasReq* pThis, const char* sPath)
{
	char* sText = DasReq_text(pThis);
	if(sText == NULL){
		daslog_error("Couldn't allocate the requisition text");
		return DASERR_SRC;
	}
	FILE* pOut = fopen(sPath, "wb");
	if(pOut == NULL){
		daslog_error_v("Couldn't open %s for writing", sPath);
		free(sText);
		return DASERR_SRC;
	}
	bool bOk = (fputs(sText, pOut) >= 0);
	fclose(pOut);
	free(sText);
	if(!bOk){
		daslog_error_v("Couldn't write %s", sPath);
		return DASERR_SRC;
	}
	return DAS_OKAY;
}

/* Example settings in nodes are written as JSON paths,
   "coords/time/props/min".  Saved requisitions may use either that or
   the dotted key, so a slash path is folded to a key on the way in:
   drop "props", join with dots.  Section aliases are left for the
   source's matcher, which accepts them anyway. */
static void _dasreq_pathToKey(const char* sPath, char* sKey, size_t uLen)
{
	size_t u = 0;
	const char* pBeg = sPath;
	sKey[0] = '\0';
	while(*pBeg && (u + 2 < uLen)){
		const char* pEnd = strchr(pBeg, '/');
		size_t n = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		if((n != 5)||(strncmp(pBeg, "props", 5) != 0)){
			if(n > uLen - u - 2) n = uLen - u - 2;
			if(u > 0) sKey[u++] = '.';
			memcpy(sKey + u, pBeg, n);
			u += n;
			sKey[u] = '\0';
		}
		if(pEnd == NULL) break;
		pBeg = pEnd + 1;
	}
}

static const char* _dasreq_jstr(const DasJdo* p, const char* sPath)
{
	const DasJdo* pSub = p ? DasJdo_get(p, sPath) : NULL;
	return ((pSub != NULL)&&(pSub->type == das_json_type_str)) ? DasJdo_string(pSub) : NULL;
}

DasReq* new_DasReq_text(const char* sJson, size_t uLen)
{
	DasJdo* pDoc = das_json_parse(sJson, uLen);
	if((pDoc == NULL)||(pDoc->type != das_json_type_dict)){
		daslog_error("Requisition text is not a JSON object");
		free(pDoc);
		return NULL;
	}
	DasReq* pThis = new_DasReq();
	if(pThis == NULL){ free(pDoc); return NULL; }

	const DasJdo* pSrc = DasJdo_get(pDoc, "source");
	DasReq_setSrc(pThis, _dasreq_jstr(pSrc, "path"), _dasreq_jstr(pSrc, "url"), _dasreq_jstr(pSrc, "version"));
	DasReq_setLabel(pThis, _dasreq_jstr(pDoc, "label"));

	char sKey[256], sVal[1024];
	bool bOk = true;
	const DasJdo* pSet = DasJdo_get(pDoc, "settings");
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pSet); bOk && pEl; pEl = pEl->next){
		if(strchr(pEl->name->string, '/')) _dasreq_pathToKey(pEl->name->string, sKey, sizeof(sKey));
		else{ strncpy(sKey, pEl->name->string, sizeof(sKey) - 1); sKey[sizeof(sKey)-1] = '\0'; }
		if(!_das_json_scalar(pEl->value, sVal, sizeof(sVal))) sVal[0] = '\0';
		bOk = (DasReq_set(pThis, sKey, sVal) == DAS_OKAY);
	}
	const DasJdo* pRaw = DasJdo_get(pDoc, "params");
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pRaw); bOk && pEl; pEl = pEl->next){
		if(!_das_json_scalar(pEl->value, sVal, sizeof(sVal))) sVal[0] = '\0';
		bOk = (DasReq_setRaw(pThis, pEl->name->string, sVal) == DAS_OKAY);
	}
	free(pDoc);
	if(!bOk){ del_DasReq(pThis); return NULL; }
	return pThis;
}

DasReq* new_DasReq_file(const char* sPath)
{
	FILE* pIn = fopen(sPath, "rb");
	if(pIn == NULL){
		daslog_error_v("Couldn't open requisition file %s", sPath);
		return NULL;
	}
	fseek(pIn, 0, SEEK_END);
	long nLen = ftell(pIn);
	fseek(pIn, 0, SEEK_SET);
	if(nLen < 0){ fclose(pIn); daslog_error_v("Couldn't size %s", sPath); return NULL; }
	char* sBuf = (char*)malloc((size_t)nLen + 1);
	if(sBuf == NULL){ fclose(pIn); return NULL; }
	size_t uRead = fread(sBuf, 1, (size_t)nLen, pIn);
	fclose(pIn);
	sBuf[uRead] = '\0';
	DasReq* pThis = new_DasReq_text(sBuf, uRead);
	free(sBuf);
	return pThis;
}
