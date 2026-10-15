/* Copyright (C) 2026  Chris Piker <chris-piker@uiowa.edu>
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

/* *************************************************************************
  
   das3_from_das2: Up-convert a das2 stream to das3 with optional composite
     variable creation

**************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>
#include <das3/tomlc17.h>

#define PROG "das3_from_das2"
#define PERR (DASERR_MAX + 1)

/* ************************************************************************* */
void prnHelp()
{
   printf(
"SYNOPSIS\n"
"   " PROG " - upconvert das2 streams, optionally adding metadata\n"
"\n"
"USAGE\n"
"   " PROG " [-m META_FILE] [-D NAME=VALUE ...] < INFILE > OUTFILE\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a filter, it reads a das2 stream on standard input and\n"
"   writes a das3 stream on standard output.  Only the headers are rewritten,\n"
"   data bytes pass through unaltered.  Compared to das2 streams, das3 streams\n"
"   provide a much richer set of metadata to identify the relevant mathematical\n"
"   formalisms of collections of values.  Das3 has the concept of external,\n"
"   bulk-dataset array indexes and internal indices for composite values.  For\n"
"   example in das2 a stream of time dependent vector values such as:\n"
"   \n"
"     time[0] lat[0] long[0], altitude[0]\n"
"     time[1] lat[1] long[1], altitude[1]\n"
"     time[2] lat[2] long[2], altitude[2]\n"
"   \n"
"   are read as one independent variable (Time) and three dependent variables\n"
"   (lat, long, altitude) with no relation between them.  In a das3 stream,\n"
"   these can be explicitly denoted as a geographic vector in standard\n"
"   coordinates (φ, λ, h) above a denoted reference body with altitudes defined\n"
"   by a specific reference surface.\n"
"\n"
"   Obviously the extra das3 metadata must come from somewhere.  When run with\n"
"   `-m` a META_FILE can be used to alter the headers output for each dataset.\n"
"   Metafiles may add simple items such as extra stream properties, or you can\n"
"   use them to group scalars in defined formalisms such as vectors, rotations,\n"
"   quaternions, etc.\n"
"\n"
"META_FILE\n"
"   Extra metadata are provided in TOML (Tom's Obvious, Minimal Language) files\n"
"   A short example is given below as an introduction.  See das3_metafile(5) for\n"
"   the full vocabulary, and examples/ex11_juno_fgm_scse.toml in the das2C\n"
"   sources for a complete, commented metafile.\n"
"\n"
"      ---------------------------------------------------------------------\n"
"      # Group mag components into a vector in a payload frame\n"
"      [[dataset.dim]]\n"
"      physDim = \"B\"\n"
"\n"
"      [[dataset.dim.var]]\n"
"      match = [\n"
"         { element = \"y\", name = \"Bx\" },\n"
"         { element = \"y\", name = \"By\" },\n"
"         { element = \"y\", name = \"Bz\" },\n"
"      ]\n"
"      ops = { kind = \"vector\", frame = \"JUNO_FGM_OB\", body = \"JUNO\" }\n"
"      ---------------------------------------------------------------------\n"
"\n"
"   Only values under a `match` key use das2 vocabulary (packet IDs, XML\n"
"   element names and plane names), everything else names das3 output items.\n"
"   Planes grouped into one variable must be adjacent in the packet and share\n"
"   an encoding, units and fill value.  If a grouping would require re-encoding\n"
"   the data, " PROG " stops with an error.  Since there are no\n"
"   calculations in hot loops, " PROG " is rather fast.\n"
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL, --log=LEVEL\n"
"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"               'warning', 'error' in order of decreasing verbosity. All log\n"
"               messages go to the standard error channel. Defaults to 'info'.\n"
"\n"
"   -m META_FILE, --meta-file=META_FILE\n"
"               Provide header metadata over and above the default upconvert\n"
"               output.  See das3_metafile(5) for details.\n"
"\n"
"   -D NAME=VALUE, --define=NAME=VALUE\n"
"               Substitute VALUE for every occurrence of ${NAME} in META_FILE\n"
"               before it is parsed.  May be repeated.  Useful when a reader\n"
"               chooses its output frame at run time, so one metafile can\n"
"               serve every frame it emits.\n"
"\n"
"EXAMPLES\n"
"   1. Upconvert Juno FGM data to a das3 vector stream and write it as text:\n"
"\n"
"      fgm_l3_rdr 2016-01-01 2016-01-02 | " PROG " -m my_meta.toml \\\n"
"         | das3_text\n"
"\n"
"   2. Write TRACERS Search Coil spectral densities to a simple CDF file:\n"
"\n"
"      msc_reader 2026-05-04T12:00 2026-05-05T18:00 | das2_psd 1024 1 \\\n"
"         | " PROG " | das3_cdf -o msc_psd.cdf\n"
"\n"
"MAINTAINER\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das3_spice, das3_text, das3_cdf, das2_from_cdf, das3_from_cdf\n"
"\n"
);
}

/* ************************************************************************* */
/* Limits.  A metafile is hand written, so small fixed tables are fine and keep
   the loader free of allocation bookkeeping. */

#define MF_MAX_DEFS   32   /* -D substitutions */
#define MF_MAX_DS     16   /* [[dataset]] sections */
#define MF_MAX_DIMS    8   /* [[dataset.dim]] per section */
#define MF_MAX_VARS    4   /* [[dataset.dim.var]] per dim */
#define MF_MAX_COMPS  16   /* planes per composite, 9 covers a matrix */
#define MF_ELEM_SZ     8   /* "yscan" plus room */

/* ************************************************************************* */
/* The metafile as parsed.  Property and ops tables are kept as references
   into the TOML tree, which lives until exit, so there is nothing to copy. */

typedef struct mf_plane {
	char sElement[MF_ELEM_SZ];      /* das2 element: x, y, z or yscan */
	char sName[DAS_MAX_ID_BUFSZ];   /* the plane's name attribute */
	int  iPlane;                    /* resolved index in the packet, per packet */
} MfPlane;

typedef struct mf_var {
	char sRole[DASDIM_ROLE_SZ];
	int nComps;
	MfPlane aComps[MF_MAX_COMPS];   /* in component order */
	toml_datum_t props;             /* TOML_TABLE or TOML_UNKNOWN */
	toml_datum_t ops;               /* TOML_TABLE, always present */
} MfVar;

typedef struct mf_dim {
	char sPhysDim[DAS_MAX_ID_BUFSZ];
	char sName[DAS_MAX_ID_BUFSZ];   /* empty means use physDim */
	toml_datum_t props;
	int nVars;
	MfVar aVars[MF_MAX_VARS];
} MfDim;

typedef struct mf_ds {
	int  lineno;
	bool bHasId;
	int  nPktId;
	char sName[DAS_MAX_ID_BUFSZ];
	toml_datum_t props;
	int nDims;
	MfDim aDims[MF_MAX_DIMS];
} MfDs;

typedef struct metafile {
	toml_result_t toml;             /* owns every string the structs point at */
	toml_datum_t props;             /* stream properties */
	int nDs;
	MfDs aDs[MF_MAX_DS];
} Metafile;

/* ************************************************************************* */
/* Program state shared by the stream handlers */

typedef struct context {
	DasIO* pOut;
	DasStream* pSdOut;
	Metafile* pMf;                  /* NULL without -m */
	char sMetaFile[256];
	char aLevel[16];
	int nDefs;
	const char* aDefName[MF_MAX_DEFS];
	const char* aDefVal[MF_MAX_DEFS];
} Context;

/* ************************************************************************* */
/* Metafile loading: text, substitutions, parse */

static char* _readFile(const char* sPath, size_t* pLen)
{
	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL){
		das_error(PERR, "Couldn't open metafile %s", sPath);
		return NULL;
	}
	size_t uSz = 0, uRead = 0;
	char* sBuf = NULL;
	for(;;){
		uSz += 4096;
		char* p = (char*)realloc(sBuf, uSz + 1);
		if(p == NULL){ free(sBuf); fclose(pFile); das_error(PERR, "Out of memory"); return NULL; }
		sBuf = p;
		size_t n = fread(sBuf + uRead, 1, uSz - uRead, pFile);
		uRead += n;
		if(uRead < uSz) break;
	}
	fclose(pFile);
	sBuf[uRead] = '\0';
	*pLen = uRead;
	return sBuf;
}

/* Replace each ${NAME} with its -D value.  Done on the text before parsing so
   a value may stand anywhere a TOML value can, inside quotes or not. */
static char* _substitute(const Context* pCtx, const char* sIn, size_t uIn, size_t* pOut)
{
	size_t uCap = uIn + 256, uLen = 0;
	char* sOut = (char*)malloc(uCap);
	if(sOut == NULL){ das_error(PERR, "Out of memory"); return NULL; }

	const char* p = sIn;
	const char* pEnd = sIn + uIn;
	while(p < pEnd){
		const char* pVar = strstr(p, "${");
		if(pVar == NULL) pVar = pEnd;

		const char* pVal = NULL;
		size_t uVal = 0;
		const char* pNext = pEnd;
		if(pVar < pEnd){
			const char* pClose = strchr(pVar + 2, '}');
			if(pClose == NULL){
				free(sOut);
				das_error(PERR, "Metafile: unterminated ${ substitution");
				return NULL;
			}
			size_t uName = pClose - (pVar + 2);
			for(int i = 0; i < pCtx->nDefs; ++i){
				if((strlen(pCtx->aDefName[i]) == uName) &&
				   (strncmp(pCtx->aDefName[i], pVar + 2, uName) == 0)){
					pVal = pCtx->aDefVal[i];
					break;
				}
			}
			if(pVal == NULL){
				free(sOut);
				das_error(PERR, "Metafile: no -D value given for ${%.*s}", (int)uName, pVar + 2);
				return NULL;
			}
			uVal = strlen(pVal);
			pNext = pClose + 1;
		}

		size_t uCopy = pVar - p;
		if(uLen + uCopy + uVal + 1 > uCap){
			uCap = (uLen + uCopy + uVal + 1) * 2;
			char* pNew = (char*)realloc(sOut, uCap);
			if(pNew == NULL){ free(sOut); das_error(PERR, "Out of memory"); return NULL; }
			sOut = pNew;
		}
		memcpy(sOut + uLen, p, uCopy);  uLen += uCopy;
		if(uVal){ memcpy(sOut + uLen, pVal, uVal); uLen += uVal; }
		p = pNext;
	}
	sOut[uLen] = '\0';
	*pOut = uLen;
	return sOut;
}

/* ************************************************************************* */
/* Metafile walking.  Every section rejects keys it does not know, the same
   policy the forms apply to <ops> attributes: a misspelling must not pass. */

static DasErrCode _mfErr(toml_datum_t d, const char* sMsg, const char* sArg)
{
	if(sArg != NULL)
		return das_error(PERR, "Metafile line %d: %s '%s'", d.lineno, sMsg, sArg);
	return das_error(PERR, "Metafile line %d: %s", d.lineno, sMsg);
}

static DasErrCode _mfCheckKeys(toml_datum_t tab, const char* sSection, const char** psAllowed)
{
	for(int i = 0; i < tab.u.tab.size; ++i){
		bool bOk = false;
		for(int j = 0; psAllowed[j] != NULL; ++j)
			if(strcmp(tab.u.tab.key[i], psAllowed[j]) == 0){ bOk = true; break; }
		if(!bOk){
			char sMsg[128];
			snprintf(sMsg, 127, "unknown key in %s:", sSection);
			return _mfErr(tab.u.tab.value[i], sMsg, tab.u.tab.key[i]);
		}
	}
	return DAS_OKAY;
}

/* Copy a string key into a fixed buffer.  A missing optional key leaves the
   buffer empty. */
static DasErrCode _mfGetStr(
	toml_datum_t tab, const char* sKey, bool bRequired, char* sDest, size_t uDest
){
	toml_datum_t d = toml_get(tab, sKey);
	if(d.type == TOML_UNKNOWN){
		if(bRequired) return _mfErr(tab, "missing required key", sKey);
		sDest[0] = '\0';
		return DAS_OKAY;
	}
	if(d.type != TOML_STRING) return _mfErr(d, "expected a string for", sKey);
	if(d.u.str.len >= (int)uDest) return _mfErr(d, "value too long for", sKey);
	strncpy(sDest, d.u.s, uDest - 1);
	sDest[uDest - 1] = '\0';
	return DAS_OKAY;
}

/* An optional table valued key.  Returns TOML_UNKNOWN when absent. */
static DasErrCode _mfGetTab(toml_datum_t tab, const char* sKey, toml_datum_t* pDest)
{
	*pDest = toml_get(tab, sKey);
	if((pDest->type != TOML_UNKNOWN)&&(pDest->type != TOML_TABLE))
		return _mfErr(*pDest, "expected a table for", sKey);
	return DAS_OKAY;
}

static DasErrCode _mfVar(MfVar* pVar, toml_datum_t tab)
{
	static const char* psKeys[] = {"match", "role", "props", "ops", NULL};
	DasErrCode nRet;
	if((nRet = _mfCheckKeys(tab, "[[dataset.dim.var]]", psKeys)) != DAS_OKAY) return nRet;

	toml_datum_t match = toml_get(tab, "match");
	if(match.type != TOML_ARRAY)
		return _mfErr(tab, "a var needs a match list of planes to group", NULL);
	if(match.u.arr.size < 1)
		return _mfErr(match, "empty match list", NULL);
	if(match.u.arr.size > MF_MAX_COMPS)
		return _mfErr(match, "more components than supported in match", NULL);

	static const char* psPlaneKeys[] = {"element", "name", NULL};
	for(int i = 0; i < match.u.arr.size; ++i){
		toml_datum_t ent = match.u.arr.elem[i];
		if(ent.type != TOML_TABLE)
			return _mfErr(ent, "each match entry is a table, as in { element = \"y\", name = \"Bx\" }", NULL);
		if((nRet = _mfCheckKeys(ent, "a match entry", psPlaneKeys)) != DAS_OKAY) return nRet;
		MfPlane* pPl = pVar->aComps + i;
		if((nRet = _mfGetStr(ent, "element", true, pPl->sElement, MF_ELEM_SZ)) != DAS_OKAY) return nRet;
		if((nRet = _mfGetStr(ent, "name", true, pPl->sName, DAS_MAX_ID_BUFSZ)) != DAS_OKAY) return nRet;
		if(strcmp(pPl->sElement, "y") != 0)
			return _mfErr(ent, "only <y> planes can be grouped in this version, not element", pPl->sElement);
		pPl->iPlane = -1;
	}
	pVar->nComps = match.u.arr.size;

	if((nRet = _mfGetStr(tab, "role", false, pVar->sRole, DASDIM_ROLE_SZ)) != DAS_OKAY) return nRet;
	if(pVar->sRole[0] == '\0') strcpy(pVar->sRole, DASVAR_CENTER);

	if((nRet = _mfGetTab(tab, "props", &(pVar->props))) != DAS_OKAY) return nRet;
	if((nRet = _mfGetTab(tab, "ops", &(pVar->ops))) != DAS_OKAY) return nRet;
	if(pVar->ops.type != TOML_TABLE)
		return _mfErr(tab, "a var needs an ops table saying what kind of value it is", NULL);
	if(toml_get(pVar->ops, "kind").type != TOML_STRING)
		return _mfErr(pVar->ops, "ops needs a kind, as in kind = \"vector\"", NULL);
	for(int i = 0; i < pVar->ops.u.tab.size; ++i)
		if(pVar->ops.u.tab.value[i].type != TOML_STRING)
			return _mfErr(pVar->ops.u.tab.value[i], "ops values are strings, they become <ops> attributes:", pVar->ops.u.tab.key[i]);
	return DAS_OKAY;
}

static DasErrCode _mfDim(MfDim* pDim, toml_datum_t tab)
{
	static const char* psKeys[] = {"physDim", "name", "props", "var", NULL};
	DasErrCode nRet;
	if((nRet = _mfCheckKeys(tab, "[[dataset.dim]]", psKeys)) != DAS_OKAY) return nRet;
	if((nRet = _mfGetStr(tab, "physDim", true, pDim->sPhysDim, DAS_MAX_ID_BUFSZ)) != DAS_OKAY) return nRet;
	if((nRet = _mfGetStr(tab, "name", false, pDim->sName, DAS_MAX_ID_BUFSZ)) != DAS_OKAY) return nRet;
	if((nRet = _mfGetTab(tab, "props", &(pDim->props))) != DAS_OKAY) return nRet;

	toml_datum_t vars = toml_get(tab, "var");
	if(vars.type != TOML_ARRAY)
		return _mfErr(tab, "a dim needs at least one [[dataset.dim.var]]", NULL);
	if(vars.u.arr.size > MF_MAX_VARS)
		return _mfErr(vars, "too many vars in one dim", NULL);
	for(int i = 0; i < vars.u.arr.size; ++i){
		if(vars.u.arr.elem[i].type != TOML_TABLE)
			return _mfErr(vars.u.arr.elem[i], "var entries must be tables", NULL);
		if((nRet = _mfVar(pDim->aVars + i, vars.u.arr.elem[i])) != DAS_OKAY) return nRet;
	}
	pDim->nVars = vars.u.arr.size;
	return DAS_OKAY;
}

static DasErrCode _mfDataset(MfDs* pDs, toml_datum_t tab)
{
	static const char* psKeys[] = {"match", "name", "props", "dim", NULL};
	DasErrCode nRet;
	pDs->lineno = tab.lineno;
	if((nRet = _mfCheckKeys(tab, "[[dataset]]", psKeys)) != DAS_OKAY) return nRet;

	toml_datum_t match = toml_get(tab, "match");
	if(match.type != TOML_UNKNOWN){
		static const char* psMatchKeys[] = {"packetId", NULL};
		if(match.type != TOML_TABLE)
			return _mfErr(match, "dataset match is a table, as in match = { packetId = 1 }", NULL);
		if((nRet = _mfCheckKeys(match, "dataset match", psMatchKeys)) != DAS_OKAY) return nRet;
		toml_datum_t id = toml_get(match, "packetId");
		if(id.type != TOML_INT64) return _mfErr(match, "packetId must be an integer", NULL);
		if((id.u.int64 < 1)||(id.u.int64 > 99)) return _mfErr(id, "packetId out of range 1 to 99", NULL);
		pDs->bHasId = true;
		pDs->nPktId = (int)id.u.int64;
	}
	if((nRet = _mfGetStr(tab, "name", false, pDs->sName, DAS_MAX_ID_BUFSZ)) != DAS_OKAY) return nRet;
	if((nRet = _mfGetTab(tab, "props", &(pDs->props))) != DAS_OKAY) return nRet;

	toml_datum_t dims = toml_get(tab, "dim");
	if(dims.type == TOML_UNKNOWN) return DAS_OKAY;   /* rename or props only */
	if(dims.type != TOML_ARRAY) return _mfErr(dims, "dim must be given as [[dataset.dim]] sections", NULL);
	if(dims.u.arr.size > MF_MAX_DIMS) return _mfErr(dims, "too many dims in one dataset", NULL);
	for(int i = 0; i < dims.u.arr.size; ++i){
		if(dims.u.arr.elem[i].type != TOML_TABLE)
			return _mfErr(dims.u.arr.elem[i], "dim entries must be tables", NULL);
		if((nRet = _mfDim(pDs->aDims + i, dims.u.arr.elem[i])) != DAS_OKAY) return nRet;
	}
	pDs->nDims = dims.u.arr.size;
	return DAS_OKAY;
}

static DasErrCode _mfLoad(Context* pCtx)
{
	size_t uLen = 0;
	char* sRaw = _readFile(pCtx->sMetaFile, &uLen);
	if(sRaw == NULL) return PERR;

	size_t uText = 0;
	char* sText = _substitute(pCtx, sRaw, uLen, &uText);
	free(sRaw);
	if(sText == NULL) return PERR;

	Metafile* pMf = (Metafile*)calloc(1, sizeof(Metafile));
	if(pMf == NULL){ free(sText); return das_error(PERR, "Out of memory"); }
	pCtx->pMf = pMf;

	pMf->toml = toml_parse_named(sText, (int)uText, pCtx->sMetaFile);
	free(sText);
	if(!pMf->toml.ok)
		return das_error(PERR, "Metafile %s: %s", pCtx->sMetaFile, pMf->toml.errmsg);

	toml_datum_t root = pMf->toml.toptab;
	static const char* psKeys[] = {"props", "dataset", NULL};
	DasErrCode nRet;
	if((nRet = _mfCheckKeys(root, "the top level", psKeys)) != DAS_OKAY) return nRet;
	if((nRet = _mfGetTab(root, "props", &(pMf->props))) != DAS_OKAY) return nRet;

	/* A single section may omit its [[dataset]] header, in which case TOML
	   hands us a plain table instead of a one element array. */
	toml_datum_t ds = toml_get(root, "dataset");
	if(ds.type == TOML_UNKNOWN) return DAS_OKAY;
	if(ds.type == TOML_TABLE){
		pMf->nDs = 1;
		return _mfDataset(pMf->aDs, ds);
	}
	if(ds.type != TOML_ARRAY) return _mfErr(ds, "dataset must be given as [[dataset]] sections", NULL);
	if(ds.u.arr.size > MF_MAX_DS) return _mfErr(ds, "too many dataset sections", NULL);
	for(int i = 0; i < ds.u.arr.size; ++i){
		if(ds.u.arr.elem[i].type != TOML_TABLE)
			return _mfErr(ds.u.arr.elem[i], "dataset entries must be tables", NULL);
		if((nRet = _mfDataset(pMf->aDs + i, ds.u.arr.elem[i])) != DAS_OKAY) return nRet;
	}
	pMf->nDs = ds.u.arr.size;
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Properties from TOML values.  The TOML type picks the das3 property type:
   a scalar is a single, a homogeneous array is a set, and a table carries
   units or a min/max pair. */

static DasErrCode _tomlTimeStr(toml_datum_t d, char* sBuf, size_t uLen)
{
	if((d.type == TOML_DATETIMETZ) && (d.u.ts.tz != 0))
		return _mfErr(d, "only UTC (Z) datetimes are accepted", NULL);
	if(d.type == TOML_DATE)
		snprintf(sBuf, uLen, "%04d-%02d-%02d", d.u.ts.year, d.u.ts.month, d.u.ts.day);
	else
		snprintf(sBuf, uLen, "%04d-%02d-%02dT%02d:%02d:%02d.%06d", d.u.ts.year,
			d.u.ts.month, d.u.ts.day, d.u.ts.hour, d.u.ts.minute, d.u.ts.second, d.u.ts.usec);
	return DAS_OKAY;
}

/* One scalar as text, with the item type it implies */
static DasErrCode _tomlScalar(toml_datum_t d, char* sBuf, size_t uLen, ubyte* pType)
{
	switch(d.type){
	case TOML_STRING:  *pType = DASPROP_STRING; snprintf(sBuf, uLen, "%s", d.u.s); return DAS_OKAY;
	case TOML_INT64:   *pType = DASPROP_INT;    snprintf(sBuf, uLen, "%lld", (long long)d.u.int64); return DAS_OKAY;
	case TOML_FP64:    *pType = DASPROP_REAL;   snprintf(sBuf, uLen, "%.17g", d.u.fp64); return DAS_OKAY;
	case TOML_BOOLEAN: *pType = DASPROP_BOOL;   snprintf(sBuf, uLen, "%s", d.u.boolean ? "true" : "false"); return DAS_OKAY;
	case TOML_DATE: case TOML_DATETIME: case TOML_DATETIMETZ:
		*pType = DASPROP_DATETIME;
		return _tomlTimeStr(d, sBuf, uLen);
	default:
		return _mfErr(d, "value type not usable as a property", NULL);
	}
}

#define PROP_VAL_SZ 4096

static DasErrCode _propFromArray(
	DasDesc* pDesc, const char* sName, toml_datum_t arr, das_units units
){
	if(arr.u.arr.size < 1) return _mfErr(arr, "empty array for property", sName);
	char* sVal = (char*)calloc(PROP_VAL_SZ, 1);
	if(sVal == NULL) return das_error(PERR, "Out of memory");
	char sItem[256];
	ubyte uType = 0, uFirst = 0;
	size_t uLen = 0;
	DasErrCode nRet = DAS_OKAY;
	for(int i = 0; i < arr.u.arr.size; ++i){
		if((nRet = _tomlScalar(arr.u.arr.elem[i], sItem, 256, &uType)) != DAS_OKAY) break;
		if(i == 0) uFirst = uType;
		else if(uType != uFirst){
			nRet = _mfErr(arr.u.arr.elem[i], "mixed types in array for property", sName);
			break;
		}
		size_t uItem = strlen(sItem);
		if(uLen + uItem + 2 > PROP_VAL_SZ){ nRet = _mfErr(arr, "array too long for property", sName); break; }
		if(i > 0) sVal[uLen++] = ';';
		memcpy(sVal + uLen, sItem, uItem);
		uLen += uItem;
	}
	if(nRet == DAS_OKAY)
		nRet = DasDesc_flexSet(pDesc, NULL, uFirst|DASPROP_SET, sName, sVal, ';', units, DASPROP_DAS3);
	free(sVal);
	return nRet;
}

static DasErrCode _propFromTable(DasDesc* pDesc, const char* sName, toml_datum_t tab)
{
	static const char* psKeys[] = {"value", "min", "max", "units", NULL};
	DasErrCode nRet;
	if((nRet = _mfCheckKeys(tab, "a property table", psKeys)) != DAS_OKAY) return nRet;

	das_units units = NULL;
	toml_datum_t u = toml_get(tab, "units");
	if(u.type != TOML_UNKNOWN){
		if(u.type != TOML_STRING) return _mfErr(u, "units must be a string for property", sName);
		units = Units_fromStr(u.u.s);
	}

	toml_datum_t v = toml_get(tab, "value");
	toml_datum_t lo = toml_get(tab, "min");
	toml_datum_t hi = toml_get(tab, "max");
	if(v.type != TOML_UNKNOWN){
		if((lo.type != TOML_UNKNOWN)||(hi.type != TOML_UNKNOWN))
			return _mfErr(tab, "give value, or min and max, not both for property", sName);
		if(v.type == TOML_ARRAY) return _propFromArray(pDesc, sName, v, units);
		char sVal[256]; ubyte uType;
		if((nRet = _tomlScalar(v, sVal, 256, &uType)) != DAS_OKAY) return nRet;
		return DasDesc_flexSet(pDesc, NULL, uType|DASPROP_SINGLE, sName, sVal, 0, units, DASPROP_DAS3);
	}
	if((lo.type == TOML_UNKNOWN)||(hi.type == TOML_UNKNOWN))
		return _mfErr(tab, "a property table needs value, or both min and max, for", sName);
	char sLo[256], sHi[256], sVal[520];
	ubyte uLo, uHi;
	if((nRet = _tomlScalar(lo, sLo, 256, &uLo)) != DAS_OKAY) return nRet;
	if((nRet = _tomlScalar(hi, sHi, 256, &uHi)) != DAS_OKAY) return nRet;
	if((uLo != uHi)||(uLo == DASPROP_STRING)||(uLo == DASPROP_BOOL))
		return _mfErr(tab, "min and max must both be numbers or both datetimes for", sName);
	snprintf(sVal, 519, "%s to %s", sLo, sHi);
	return DasDesc_flexSet(pDesc, NULL, uLo|DASPROP_RANGE, sName, sVal, 0, units, DASPROP_DAS3);
}

static DasErrCode _applyProps(DasDesc* pDesc, toml_datum_t tab)
{
	if(tab.type != TOML_TABLE) return DAS_OKAY;
	DasErrCode nRet = DAS_OKAY;
	for(int i = 0; (i < tab.u.tab.size) && (nRet == DAS_OKAY); ++i){
		const char* sName = tab.u.tab.key[i];
		toml_datum_t v = tab.u.tab.value[i];
		switch(v.type){
		case TOML_ARRAY: nRet = _propFromArray(pDesc, sName, v, NULL); break;
		case TOML_TABLE: nRet = _propFromTable(pDesc, sName, v); break;
		default: {
			char sVal[256]; ubyte uType;
			if((nRet = _tomlScalar(v, sVal, 256, &uType)) != DAS_OKAY) break;
			nRet = DasDesc_flexSet(pDesc, NULL, uType|DASPROP_SINGLE, sName, sVal, 0, NULL, DASPROP_DAS3);
		}
		}
	}
	return nRet;
}

/* ************************************************************************* */
/* Section selection.  A section applies to a packet when its packet ID
   matches, if it names one, and every plane its vars group is present with
   the right element type.  A named ID whose planes are missing is a mistake
   the user wants to hear about, not a silent non-match. */

static DasErrCode _resolveVar(MfVar* pVar, PktDesc* pPd, bool bLoud, bool* pFound)
{
	*pFound = true;
	for(int i = 0; i < pVar->nComps; ++i){
		MfPlane* pPl = pVar->aComps + i;
		int iPlane = PktDesc_getPlaneIdxByName(pPd, pPl->sName, Y);
		if(iPlane < 0){
			*pFound = false;
			if(bLoud)
				return das_error(PERR, "Packet %02d has no <%s> plane named '%s'",
					PktDesc_getId(pPd), pPl->sElement, pPl->sName);
			return DAS_OKAY;
		}
		pPl->iPlane = iPlane;
	}
	return DAS_OKAY;
}

static DasErrCode _selectSection(Metafile* pMf, PktDesc* pPd, MfDs** ppSec)
{
	*ppSec = NULL;
	int nPktId = PktDesc_getId(pPd);
	for(int s = 0; s < pMf->nDs; ++s){
		MfDs* pSec = pMf->aDs + s;
		if(pSec->bHasId && (pSec->nPktId != nPktId)) continue;

		bool bAll = true;
		for(int d = 0; (d < pSec->nDims) && bAll; ++d){
			for(int v = 0; (v < pSec->aDims[d].nVars) && bAll; ++v){
				bool bFound;
				DasErrCode nRet = _resolveVar(pSec->aDims[d].aVars + v, pPd, pSec->bHasId, &bFound);
				if(nRet != DAS_OKAY) return nRet;
				bAll = bFound;
			}
		}
		if(!bAll) continue;

		if(*ppSec != NULL)
			return das_error(PERR,
				"Metafile sections at lines %d and %d both apply to packet %02d",
				(*ppSec)->lineno, pSec->lineno, nPktId
			);
		*ppSec = pSec;
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Building the altered dataset.  The default up-conversion gives one array,
   one variable and one dimension per plane, with codecs in packet order.  The
   output is rebuilt in that same codec order, so the wire layout is kept, and
   at the first plane of each group a composite stands in for the run of
   scalars.  Nothing is ever decoded, so the composite's array stays empty;
   it exists to carry the type, units, fill and shape into the header. */

/* The up-converter's array id for a plane: the name with dots replaced */
static void _planeAryId(const PlaneDesc* pPlane, char* sBuf, size_t uLen)
{
	strncpy(sBuf, PlaneDesc_getName(pPlane), uLen - 1);
	sBuf[uLen - 1] = '\0';
	for(char* p = sBuf; *p; ++p) if(*p == '.') *p = '_';
}

/* The input dimension whose variable reads a given array, with its var count */
static DasDim* _dimForAry(DasDs* pDs, const DasAry* pAry)
{
	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){
		size_t uDims = DasDs_numDims(pDs, iType);
		for(size_t uD = 0; uD < uDims; ++uD){
			DasDim* pDim = DasDs_getDimByIdx(pDs, uD, iType);
			for(size_t uV = 0; uV < DasDim_numVars(pDim); ++uV)
				if(DasVar_getAry(DasDim_getVarByIdx(pDim, uV)) == pAry) return pDim;
		}
	}
	return NULL;
}

/* Copy a dimension and its variables the way DasDs_copy() does, sharing arrays */
static DasErrCode _copyDim(DasDs* pDsOut, DasDim* pDimIn)
{
	DasDim* pDimOut = DasDs_makeDim(pDsOut, DasDim_type(pDimIn), DasDim_dim(pDimIn), DasDim_id(pDimIn));
	if(pDimOut == NULL) return PERR;
	DasDesc_copyIn((DasDesc*)pDimOut, (DasDesc*)pDimIn);
	DasDim_setAxes(pDimOut, pDimIn);
	for(size_t uV = 0; uV < DasDim_numVars(pDimIn); ++uV){
		DasVar* pVar = DasVar_copy(DasDim_getVarByIdx(pDimIn, uV));
		if(pVar == NULL) return PERR;
		if(!DasDim_addVar(pDimOut, DasDim_getRoleByIdx(pDimIn, uV), pVar)){
			dec_DasVar(pVar);
			return PERR;
		}
	}
	return DAS_OKAY;
}

/* Where each component of a group lives in the input dataset */
typedef struct comp_src {
	DasAry*   pAry;
	DasCodec* pCodec;
	int       iCodec;
	DasDim*   pDim;
} CompSrc;

/* The byte promise, checked before anything is built: the group must be a
   run of adjacent single-item planes that share encoding, width, value type,
   units and fill, so the composite's packet entry describes the same bytes. */
static DasErrCode _checkGroup(DasDs* pDsIn, PktDesc* pPd, MfVar* pVar, CompSrc* pSrc)
{
	char sId[DAS_MAX_ID_BUFSZ];
	for(int i = 0; i < pVar->nComps; ++i){
		const char* sPlane = pVar->aComps[i].sName;
		if(i > 0 && (pVar->aComps[i].iPlane != pVar->aComps[0].iPlane + i))
			return das_error(PERR,
				"Packet %02d: planes '%s' and '%s' are not adjacent, grouping them "
				"would re-encode the data", PktDesc_getId(pPd), pVar->aComps[0].sName, sPlane
			);
		_planeAryId(PktDesc_getPlane(pPd, pVar->aComps[i].iPlane), sId, DAS_MAX_ID_BUFSZ);
		pSrc[i].pAry = DasDs_getAryById(pDsIn, sId);
		if(pSrc[i].pAry == NULL)
			return das_error(PERR, "Logic error, no array for plane '%s'", sPlane);
		int nItems = 0;
		pSrc[i].pCodec = DasDs_getCodecFor(pDsIn, sId, &nItems);
		if(pSrc[i].pCodec == NULL) return PERR;
		pSrc[i].iCodec = (int)(pSrc[i].pCodec - DasDs_getCodec(pDsIn, 0));
		if(nItems != 1)
			return das_error(PERR, "Plane '%s' carries %d items per packet, expected 1", sPlane, nItems);
		pSrc[i].pDim = _dimForAry(pDsIn, pSrc[i].pAry);
		if((pSrc[i].pDim == NULL)||(DasDim_numVars(pSrc[i].pDim) != 1))
			return das_error(PERR,
				"Plane '%s' shares a dimension with other planes (a das2 'source' group), "
				"grouping it is not supported", sPlane
			);
		if(i == 0) continue;

		const CompSrc* p0 = pSrc;
		const CompSrc* pI = pSrc + i;
		if((p0->pCodec->sEncType != pI->pCodec->sEncType)||
		   (p0->pCodec->nBufValSz != pI->pCodec->nBufValSz))
			return das_error(PERR,
				"Planes '%s' and '%s' differ in encoding, grouping them would re-encode the data",
				pVar->aComps[0].sName, sPlane
			);
		if(DasAry_valType(p0->pAry) != DasAry_valType(pI->pAry))
			return das_error(PERR, "Planes '%s' and '%s' differ in value type", pVar->aComps[0].sName, sPlane);
		if(DasAry_units(p0->pAry) != DasAry_units(pI->pAry))
			return das_error(PERR,
				"Planes '%s' and '%s' differ in units (%s vs %s), a composite has one set",
				pVar->aComps[0].sName, sPlane, Units_toStr(DasAry_units(p0->pAry)),
				Units_toStr(DasAry_units(pI->pAry))
			);
		size_t uValSz = das_vt_size(DasAry_valType(p0->pAry));
		if(memcmp(DasAry_getFill(p0->pAry), DasAry_getFill(pI->pAry), uValSz) != 0)
			return das_error(PERR,
				"Planes '%s' and '%s' differ in fill value, a composite has one",
				pVar->aComps[0].sName, sPlane
			);
	}
	return DAS_OKAY;
}

/* Make the composite: array, generator, form, variable, codec, all in the
   output dataset, and hang it on the given dimension. */
static DasErrCode _addComposite(
	DasDs* pDsOut, DasDim* pDimOut, MfVar* pVar, const CompSrc* pSrc, PktDesc* pPd
){
	const DasAry* pAry0 = pSrc[0].pAry;
	int nComps = pVar->nComps;

	char sId[DAS_MAX_ID_BUFSZ];
	snprintf(sId, DAS_MAX_ID_BUFSZ - 1, "%s_%s", DasDim_id(pDimOut), pVar->sRole);

	size_t aShape[2] = {0, (size_t)nComps};
	DasAry* pAry = new_DasAry(
		sId, DasAry_valType(pAry0), 0, DasAry_getFill(pAry0), 2, aShape, DasAry_units(pAry0)
	);
	if(pAry == NULL) return PERR;
	if(DasDs_addAry(pDsOut, pAry) != DAS_OKAY){ dec_DasAry(pAry); return PERR; }
	dec_DasAry(pAry);   /* the dataset holds the surviving reference */

	if(DasDs_addFixedCodec(
		pDsOut, sId, pSrc[0].pCodec->sSemantic, pSrc[0].pCodec->sEncType,
		pSrc[0].pCodec->nBufValSz, nComps, DASENC_READ
	) == NULL)
		return PERR;

	/* The ops table becomes <ops> attributes verbatim; the form's own
	   parameter checks reject anything the kind does not understand. */
	const char* psAttr[2 * 32 + 1];
	toml_datum_t ops = pVar->ops;
	if(ops.u.tab.size > 32) return _mfErr(ops, "too many ops attributes", NULL);
	for(int i = 0; i < ops.u.tab.size; ++i){
		psAttr[2*i]   = ops.u.tab.key[i];
		psAttr[2*i+1] = ops.u.tab.value[i].u.s;
	}
	psAttr[2 * ops.u.tab.size] = NULL;

	int8_t aMap[VARIDX_MAX] = {0, VARIDX_UNUSED, VARIDX_UNUSED, VARIDX_UNUSED,
	                           VARIDX_UNUSED, VARIDX_UNUSED, VARIDX_UNUSED, VARIDX_UNUSED};
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	DasForm* pForm = new_DasForm_pairs(psAttr);
	if((pGen == NULL)||(pForm == NULL)){
		DasGen_decRef(pGen);
		del_DasForm(pForm);
		return PERR;
	}
	ptrdiff_t aIntShape[1] = {nComps};
	DasVar* pVarOut = (DasVar*) new_DasVarComp(pGen, DasAry_units(pAry0), pForm, 1, aIntShape);
	DasGen_decRef(pGen);   /* both constructors took their own reference */
	del_DasForm(pForm);
	if(pVarOut == NULL) return PERR;

	/* Component labels default to the source planes' labels, when all have one */
	bool bLabels = true;
	char sLabels[PROP_VAL_SZ] = {'\0'};
	size_t uLen = 0;
	for(int i = 0; (i < nComps) && bLabels; ++i){
		const char* sLbl = DasDesc_get((DasDesc*)pSrc[i].pDim, "label");
		if((sLbl == NULL)||(uLen + strlen(sLbl) + 2 > PROP_VAL_SZ)){ bLabels = false; break; }
		if(i > 0) sLabels[uLen++] = ';';
		strcpy(sLabels + uLen, sLbl);
		uLen += strlen(sLbl);
	}
	if(bLabels)
		DasDesc_flexSet((DasDesc*)pVarOut, NULL, DASPROP_STRING|DASPROP_SET, "label", sLabels, ';', NULL, DASPROP_DAS3);

	DasErrCode nRet = _applyProps((DasDesc*)pVarOut, pVar->props);
	if(nRet != DAS_OKAY){ dec_DasVar(pVarOut); return nRet; }

	if(!DasDim_addVar(pDimOut, pVar->sRole, pVarOut)){
		dec_DasVar(pVarOut);
		return das_error(PERR, "Couldn't add role '%s' to dimension %s", pVar->sRole, DasDim_id(pDimOut));
	}
	return DAS_OKAY;
}

static DasErrCode _buildFromSection(
	PktDesc* pPd, DasDs* pDsIn, MfDs* pSec, DasDs** ppDsOut
){
	DasErrCode nRet = DAS_OKAY;
	int nCodecs = (int)DasDs_numCodecs(pDsIn);

	/* Which input codec slots each group claims, and what it needs from them */
	CompSrc aSrc[MF_MAX_DIMS][MF_MAX_VARS][MF_MAX_COMPS];
	int* aClaim = (int*)malloc(nCodecs * sizeof(int));   /* -1 or dim*MF_MAX_VARS+var */
	if(aClaim == NULL) return das_error(PERR, "Out of memory");
	for(int i = 0; i < nCodecs; ++i) aClaim[i] = -1;

	for(int d = 0; d < pSec->nDims; ++d){
		for(int v = 0; v < pSec->aDims[d].nVars; ++v){
			MfVar* pVar = pSec->aDims[d].aVars + v;
			if((nRet = _checkGroup(pDsIn, pPd, pVar, aSrc[d][v])) != DAS_OKAY){ free(aClaim); return nRet; }
			for(int c = 0; c < pVar->nComps; ++c){
				int iCodec = aSrc[d][v][c].iCodec;
				if(aClaim[iCodec] != -1){
					free(aClaim);
					return das_error(PERR, "Plane '%s' is claimed by two groups", pVar->aComps[c].sName);
				}
				aClaim[iCodec] = d * MF_MAX_VARS + v;
			}
		}
	}

	DasDs* pDsOut = new_DasDs(DasDs_id(pDsIn), DasDs_group(pDsIn), pDsIn->nRank);
	if(pDsOut == NULL){ free(aClaim); return PERR; }
	DasDesc_copyIn((DasDesc*)pDsOut, (const DasDesc*)pDsIn);

	DasDim* aDimOut[MF_MAX_DIMS] = {NULL};  /* created on first use, in packet order */
	DasDim* aCopied[DASDS_LOC_DIM_SZ * 4];  /* input dims already carried over */
	int nCopied = 0;

	for(int i = 0; (i < nCodecs) && (nRet == DAS_OKAY); ++i){
		DasCodec* pCodec = DasDs_getCodec(pDsIn, i);
		DasAry* pAry = pCodec->pAry;

		if(aClaim[i] >= 0){
			int d = aClaim[i] / MF_MAX_VARS, v = aClaim[i] % MF_MAX_VARS;
			MfVar* pVar = pSec->aDims[d].aVars + v;
			if(aSrc[d][v][0].iCodec != i) continue;   /* not the group's first plane */

			if(aDimOut[d] == NULL){
				MfDim* pMfDim = pSec->aDims + d;
				aDimOut[d] = DasDs_makeDim(pDsOut, DASDIM_DATA, pMfDim->sPhysDim,
					pMfDim->sName[0] ? pMfDim->sName : pMfDim->sPhysDim);
				if(aDimOut[d] == NULL){ nRet = PERR; break; }
				if((nRet = _applyProps((DasDesc*)aDimOut[d], pMfDim->props)) != DAS_OKAY) break;
			}
			nRet = _addComposite(pDsOut, aDimOut[d], pVar, aSrc[d][v], pPd);
			continue;
		}

		/* Pass through: share the array, re-use the codec, copy the dim once */
		if(DasDs_addAry(pDsOut, pAry) != DAS_OKAY){ nRet = PERR; break; }
		if(DasDs_addCodecFrom(pDsOut, DasAry_id(pAry), pCodec, DasDs_pktItems(pDsIn, i),
			DasCodec_isReader(pCodec)) == NULL){ nRet = PERR; break; }

		DasDim* pDimIn = _dimForAry(pDsIn, pAry);
		if(pDimIn == NULL){ nRet = das_error(PERR, "Logic error, orphan array %s", DasAry_id(pAry)); break; }
		bool bDone = false;
		for(int k = 0; k < nCopied; ++k) if(aCopied[k] == pDimIn){ bDone = true; break; }
		if(bDone) continue;
		if(nCopied >= (int)(sizeof(aCopied)/sizeof(aCopied[0]))){ nRet = das_error(PERR, "Too many dimensions"); break; }
		aCopied[nCopied++] = pDimIn;
		nRet = _copyDim(pDsOut, pDimIn);
	}
	free(aClaim);

	if(nRet == DAS_OKAY) nRet = _applyProps((DasDesc*)pDsOut, pSec->props);
	if(nRet != DAS_OKAY){ del_DasDs(pDsOut); return nRet; }
	*ppDsOut = pDsOut;
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Stream handlers */

DasErrCode onStream(DasStream* pSdIn, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	pCtx->pSdOut = DasStream_copy(pSdIn);
	if(pCtx->pSdOut == NULL) return PERR;
	if(pCtx->pMf != NULL){
		DasErrCode nRet = _applyProps((DasDesc*)pCtx->pSdOut, pCtx->pMf->props);
		if(nRet != DAS_OKAY) return nRet;
	}
	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pCtx->pSdOut, 0);
}

DasErrCode onPktDesc(DasStream* pSdIn, PktDesc* pPd, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	int nPktId = PktDesc_getId(pPd);
	DasErrCode nRet;

	MfDs* pSec = NULL;
	if(pCtx->pMf != NULL){
		if((nRet = _selectSection(pCtx->pMf, pPd, &pSec)) != DAS_OKAY) return nRet;
	}

	const char* sName = ((pSec != NULL) && (pSec->sName[0] != '\0')) ? pSec->sName : NULL;
	DasDs* pDs = new_DasDs_packet(pSdIn, pPd, sName, true);
	if(pDs == NULL) return PERR;

	DasDs* pDsOut = pDs;
	if(pSec != NULL){
		daslog_info_v("Packet %02d: applying metafile section at line %d", nPktId, pSec->lineno);
		nRet = _buildFromSection(pPd, pDs, pSec, &pDsOut);
		del_DasDs(pDs);   /* arrays the output still uses survive on their own refs */
		if(nRet != DAS_OKAY) return nRet;
	}

	if((nRet = DasStream_addDesc(pCtx->pSdOut, (DasDesc*)pDsOut, nPktId)) != DAS_OKAY)
		return nRet;
	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pDsOut, nPktId);
}

/* The reader is about to drop the old definition of this ID; the output
   stream must vacate the slot too, or the redefinition can't be registered. */
DasErrCode onPktRedef(DasStream* pSdIn, DasDesc* pDescIn, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	int nPktId = DasStream_getPktId(pSdIn, pDescIn);
	if(DasStream_getDesc(pCtx->pSdOut, nPktId) == NULL) return DAS_OKAY;
	return DasStream_freeDatDesc(pCtx->pSdOut, nPktId);
}

/* The byte promise in one place: the payload goes out as it came in, only
   the tag changes from das2 to das3 form. */
DasErrCode onRawData(DasStream* pSd, int nPktId, DasDesc* pDesc, const DasBuf* pBuf, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	size_t uLen = 0;
	const ubyte* pData = DasBuf_direct(pBuf, &uLen);
	if(pData == NULL) return das_error(PERR, "Empty payload for packet %02d", nPktId);
	if(DasIO_printf(pCtx->pOut, "|Pd|%d|%zu|", nPktId, uLen) < 0) return PERR;
	if(DasIO_write(pCtx->pOut, (const char*)pData, (int)uLen) != uLen)
		return das_error(PERR, "Short write for packet %02d", nPktId);
	return DAS_OKAY;
}

DasErrCode onException(OobExcept* pExcept, void* pUser)
{
	return DasIO_writeException(((Context*)pUser)->pOut, pExcept);
}

DasErrCode onComment(OobComment* pCmt, void* pUser)
{
	return DasIO_writeComment(((Context*)pUser)->pOut, pCmt);
}

DasErrCode onClose(DasStream* pSdIn, void* pUser)
{
	DasIO_close(((Context*)pUser)->pOut);
	return DAS_OKAY;
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	Context ctx;
	memset(&ctx, 0, sizeof(Context));
	strcpy(ctx.aLevel, "info");

	for(int i = 1; i < argc; ++i){
		if(dascmd_isArg(argv[i], "-h", "--help", NULL)){ prnHelp(); return 0; }
		if(dascmd_getArgVal(ctx.aLevel, DAS_FIELD_SZ(Context, aLevel), argv, argc, &i, "-l", "--log="))
			continue;
		if(dascmd_getArgVal(ctx.sMetaFile, DAS_FIELD_SZ(Context, sMetaFile), argv, argc, &i, "-m", "--meta-file="))
			continue;
		char sDef[256] = {'\0'};
		if(dascmd_getArgVal(sDef, 256, argv, argc, &i, "-D", "--define=")){
			char* pEq = strchr(sDef, '=');
			if((pEq == NULL)||(pEq == sDef))
				return das_error(PERR, "-D expects NAME=VALUE, not '%s'", sDef);
			if(ctx.nDefs >= MF_MAX_DEFS) return das_error(PERR, "Too many -D options");
			*pEq = '\0';
			ctx.aDefName[ctx.nDefs] = strdup(sDef);
			ctx.aDefVal[ctx.nDefs]  = strdup(pEq + 1);
			++ctx.nDefs;
			continue;
		}
		return das_error(PERR, "Unknown command line argument '%s', use -h for help", argv[i]);
	}
	daslog_setlevel(daslog_strlevel(ctx.aLevel));

	if(ctx.sMetaFile[0] != '\0'){
		if(_mfLoad(&ctx) != DAS_OKAY) return PERR;
	}

	ctx.pOut = new_DasIO_cfile(PROG, stdout, "w3");
	DasIO* pIn = new_DasIO_cfile(PROG, stdin, "r");

	/* The das2 model keeps PktDesc objects, which is what the metafile's
	   match vocabulary names.  Each packet is up-converted by hand instead. */
	DasIO_model(pIn, STREAM_MODEL_V2);

	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.pktDescHandler    = onPktDesc;
	handler.pktRedefHandler   = onPktRedef;
	handler.rawDataHandler    = onRawData;   /* no data handler: nothing is decoded */
	handler.exceptionHandler  = onException;
	handler.commentHandler    = onComment;
	handler.closeHandler      = onClose;
	handler.userData = &ctx;
	DasIO_addProcessor(pIn, &handler);

	int nRet = DasIO_readAll(pIn);

	del_DasIO(pIn);
	if(ctx.pSdOut != NULL) del_DasStream(ctx.pSdOut);  /* and the datasets it owns */
	del_DasIO(ctx.pOut);
	if(ctx.pMf != NULL){
		toml_free(ctx.pMf->toml);
		free(ctx.pMf);
	}
	for(int i = 0; i < ctx.nDefs; ++i){
		free((char*)ctx.aDefName[i]);
		free((char*)ctx.aDefVal[i]);
	}
	return nRet;
}
