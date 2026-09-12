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
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* Read back the CDFs das3_cdf writes for composite variables and check what
   an ISTP reader sees: one trailing dimension per composite whatever its
   internal shape, a LABL_PTR naming every component in C order, a UNIT_PTR
   only where the form's law splits the units, and the numbers in the slots
   the labels claim.

   Argument: the build directory holding das3_cdf's output for examples
   ex40 through ex43.  Nothing here reads a das3 stream; the contract under
   test is the CDF file, and it is checked with the CDF library alone. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <cdf.h>

static int nErrs = 0;

#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		++nErrs; \
	}

#define CDF_OK_OR_FAIL(call) \
	if((call) != CDF_OK){ \
		printf("ERROR: CDF call failed at %s:%d: %s\n", __FILE__, __LINE__, #call); \
		++nErrs; \
		return; \
	}

/* One expectation per (file, variable).  Label and unit lists are ';' joined
   in storage order; NULL means the attribute must be absent, and a list of
   just "#N" means N entries whose text is not pinned.  sAttrs is a '|' joined
   list of name=value pairs that must be present with that value (integer
   entries compare by their decimal image); sAbsent a '|' joined list of
   attribute names that must not be set on the variable. */
typedef struct expect {
	const char* sFile;
	const char* sVar;
	long        nDim;         /* the one trailing dimension's length */
	const char* sLabels;      /* LABL_PTR_1 pointee contents */
	const char* sUnits;       /* UNIT_PTR_1 pointee contents, or NULL for none */
	bool        bHasUnitsAttr;/* plain UNITS present */
	const char* sRep;         /* REPRESENTATION_1 pointee contents, or NULL */
	const char* sAttrs;       /* required name=value pairs */
	const char* sAbsent;      /* forbidden attribute names */
	const char* sGlobals;     /* required global name=value pairs, entry 0 */
	const char* sGlobalsAbsent; /* forbidden global attribute names */
	long        iRec;         /* record to read for the value check */
	int         nVals;        /* leading values to compare, 0 to skip */
	double      aVals[9];
	double      rTol;
} expect_t;

static const expect_t g_aExpect[] = {
	/* a cartesian vector in a frame: both metadata stages, defaults left off */
	{ "ex15_vector_frame.cdf", "EAC", 3,
	  "Ex;Ey;Ez",
	  NULL, true, "x;y;z",
	  "opsKind=vector|opsFrame=TSCS|opsBody=TS1|opsSystem=cartesian|"
	  "COORDINATE_SYSTEM=TSCS|TENSOR_ORDER=1",
	  "opsSysorder|opsFixed|UNIT_PTR_1|FRAME_ORIGIN|COORD_FRAME",
	  "", "",
	  0, 0, {0.0}, 0.0 },

	/* the same file written with -p COORDINATE_SYSTEM:COORD_FRAME */
	{ "ex15_mapped.cdf", "EAC", 3,
	  "Ex;Ey;Ez",
	  NULL, true, "x;y;z",
	  "opsKind=vector|opsFrame=TSCS|COORD_FRAME=TSCS|TENSOR_ORDER=1",
	  "COORDINATE_SYSTEM",
	  "", "",
	  0, 0, {0.0}, 0.0 },

	/* a 3;3 matrix flattened to nine cells in C order */
	{ "ex40_rotation.cdf", "rot", 9,
	  "rot_xx;rot_xy;rot_xz;rot_yx;rot_yy;rot_yz;rot_zx;rot_zy;rot_zz",
	  NULL, true, NULL,
	  "opsKind=rotation|opsFrom=TSCS|opsTo=GEI2000|opsSystem=matrix|TENSOR_ORDER=2",
	  "opsSysorder|COORD_FRAME|COORDINATE_SYSTEM|REPRESENTATION_1",
	  "", "",
	  1, 9, {0.0, -1.0, 0.0,  1.0, 0.0, 0.0,  0.0, 0.0, 1.0}, 1e-9 },

	/* a quaternion declared scalar-last: sysorder kept, no tensor order */
	{ "ex41_quaternion.cdf", "q_xyzw", 4,
	  "q_xyzw_x;q_xyzw_y;q_xyzw_z;q_xyzw_w",
	  NULL, true, NULL,
	  "opsKind=rotation|opsSystem=quaternion|opsSysorder=1;2;3;0",
	  "TENSOR_ORDER",
	  "", "",
	  1, 4, {0.0, 0.0, 0.7071, 0.7071}, 1e-4 },

	/* no <ops> at all: a plain run is linear */
	{ "ex42_plain_tensor.cdf", "cov", 9, "#9",
	  NULL, true, NULL,
	  "opsKind=linear",
	  "opsFrame|COORD_FRAME|TENSOR_ORDER",
	  "", "",
	  0, 0, {0.0}, 0.0 },

	/* a kind das2C has no rules for: labels by index, parameters carried
	   through verbatim, and no ISTP claims made on its behalf */
	{ "ex42_plain_tensor.cdf", "heat_flux", 27, "#27",
	  NULL, true, NULL,
	  "opsKind=tensor|opsFrame=GSE|opsSymmetry=full",
	  "COORD_FRAME|COORDINATE_SYSTEM|TENSOR_ORDER",
	  "", "",
	  0, 6, {0.0, 1.0, 2.0, 10.0, 11.0, 12.0}, 1e-6 },

	/* rectangular complex: author's labels, one blank UNITS, no UNIT_PTR */
	{ "ex43_msc_complex_cal.cdf", "cal_rect", 2,
	  "real;imaginary",
	  NULL, true, NULL,
	  "opsKind=complex|opsSystem=rectangular",
	  "UNIT_PTR_1|TENSOR_ORDER",
	  "", "sourceId",       /* a lower-case stream property is dropped by default */
	  0, 2, {-0.05220497, -0.81409886}, 1e-6 },

	/* the same file written with -k: the lower-case global survives */
	{ "ex43_keep.cdf", "cal_rect", 2,
	  "real;imaginary",
	  NULL, true, NULL,
	  "opsKind=complex|opsSystem=rectangular",
	  "UNIT_PTR_1",
	  "sourceId=tmp/gen_ex43.py", "",
	  0, 0, {0.0}, 0.0 },

	/* polar complex: synthesized labels, degrees on the phase via UNIT_PTR,
	   and no plain UNITS since ISTP wants one or the other */
	{ "ex43_msc_complex_cal.cdf", "cal_polar", 2,
	  "cal_polar_mag;cal_polar_phase",
	  ";deg", false, NULL,
	  "opsKind=complex|opsSystem=polar",
	  "UNITS",
	  "", "",
	  0, 2, {0.815771, -93.66913}, 1e-4 },
};

/* Fetch an attribute entry for a variable into sBuf as text; false if absent.
   Character entries come back as they are, integer entries as decimal. */
static bool getStrEntry(CDFid id, long iVar, const char* sAttr, char* sBuf, size_t uLen)
{
	long iAttr = CDFgetAttrNum(id, (char*)sAttr);
	if(iAttr < 0) return false;
	if(CDFconfirmzEntryExistence(id, iAttr, iVar) != CDF_OK) return false;

	long nType = 0, nElems = 0;
	if(CDFgetAttrzEntryDataType(id, iAttr, iVar, &nType) != CDF_OK) return false;
	if(CDFgetAttrzEntryNumElements(id, iAttr, iVar, &nElems) != CDF_OK) return false;

	memset(sBuf, 0, uLen);
	if((nType == CDF_CHAR)||(nType == CDF_UCHAR)){
		if((size_t)nElems >= uLen) nElems = uLen - 1;
		if(CDFgetAttrzEntry(id, iAttr, iVar, sBuf) != CDF_OK) return false;
		sBuf[nElems] = '\0';
		return true;
	}
	if(((nType == CDF_INT4)||(nType == CDF_INT2)||(nType == CDF_INT1)||(nType == CDF_INT8))
	   &&(nElems == 1)){
		long long nVal = 0;
		if(CDFgetAttrzEntry(id, iAttr, iVar, &nVal) != CDF_OK) return false;
		if(nType == CDF_INT4) nVal = *((int32_t*)&nVal);
		if(nType == CDF_INT2) nVal = *((int16_t*)&nVal);
		if(nType == CDF_INT1) nVal = *((int8_t*)&nVal);
		snprintf(sBuf, uLen - 1, "%lld", nVal);
		return true;
	}
	snprintf(sBuf, uLen - 1, "<type %ld>", nType);
	return true;
}

/* Fetch global attribute entry 0 as text; false if absent */
static bool getGlobalEntry(CDFid id, const char* sAttr, char* sBuf, size_t uLen)
{
	long iAttr = CDFgetAttrNum(id, (char*)sAttr);
	if(iAttr < 0) return false;
	if(CDFconfirmgEntryExistence(id, iAttr, 0) != CDF_OK) return false;
	long nType = 0, nElems = 0;
	if(CDFgetAttrgEntryDataType(id, iAttr, 0, &nType) != CDF_OK) return false;
	if(CDFgetAttrgEntryNumElements(id, iAttr, 0, &nElems) != CDF_OK) return false;
	memset(sBuf, 0, uLen);
	if((nType != CDF_CHAR)&&(nType != CDF_UCHAR)){
		snprintf(sBuf, uLen - 1, "<type %ld>", nType);
		return true;
	}
	if((size_t)nElems >= uLen) nElems = uLen - 1;
	if(CDFgetAttrgEntry(id, iAttr, 0, sBuf) != CDF_OK) return false;
	sBuf[nElems] = '\0';
	return true;
}

/* Read a 1-D NRV character variable as a ';' joined list, entries right
   trimmed of the padding CDF stores them with */
static bool getStrList(CDFid id, const char* sVar, char* sOut, size_t uLen)
{
	long iVar = CDFgetVarNum(id, (char*)sVar);
	if(iVar < 0) return false;

	long nDims = 0, nElems = 0;
	long aDims[CDF_MAX_DIMS] = {0};
	if(CDFgetzVarNumDims(id, iVar, &nDims) != CDF_OK) return false;
	if(nDims != 1) return false;
	if(CDFgetzVarDimSizes(id, iVar, aDims) != CDF_OK) return false;
	if(CDFgetzVarNumElements(id, iVar, &nElems) != CDF_OK) return false;

	long n = aDims[0];
	char* pRaw = (char*)calloc(n * nElems + 1, 1);
	long aIdx[1] = {0}, aCnt[1] = {n}, aInt[1] = {1};
	if(CDFhyperGetzVarData(id, iVar, 0, 1, 1, aIdx, aCnt, aInt, pRaw) != CDF_OK){
		free(pRaw);
		return false;
	}

	sOut[0] = '\0';
	size_t uOut = 0;
	for(long i = 0; i < n; ++i){
		long nLen = nElems;
		while((nLen > 0)&&(pRaw[i*nElems + nLen - 1] == ' ')) --nLen;
		if(uOut + nLen + 2 >= uLen) break;
		if(i > 0) sOut[uOut++] = ';';
		memcpy(sOut + uOut, pRaw + i*nElems, nLen);
		uOut += nLen;
		sOut[uOut] = '\0';
	}
	free(pRaw);
	return true;
}

static void checkOne(const char* sDir, const expect_t* pE)
{
	char sPath[512] = {'\0'};
	snprintf(sPath, 511, "%s/%s", sDir, pE->sFile);
	printf("INFO: %s:%s\n", pE->sFile, pE->sVar);

	CDFid id;
	CDF_OK_OR_FAIL(CDFopenCDF(sPath, &id));

	long iVar = CDFgetVarNum(id, (char*)pE->sVar);
	CHECK(iVar >= 0);
	if(iVar < 0){ CDFcloseCDF(id); return; }

	/* one trailing dimension, whatever the internal shape was */
	long nDims = 0;
	long aDims[CDF_MAX_DIMS] = {0};
	CHECK(CDFgetzVarNumDims(id, iVar, &nDims) == CDF_OK);
	CHECK(CDFgetzVarDimSizes(id, iVar, aDims) == CDF_OK);
	CHECK(nDims == 1);
	CHECK(aDims[0] == pE->nDim);

	/* LABL_PTR_1 names a variable holding one label per component */
	char sPtr[256] = {'\0'};
	char sList[1024] = {'\0'};
	CHECK(getStrEntry(id, iVar, "LABL_PTR_1", sPtr, 256));
	CHECK(getStrList(id, sPtr, sList, 1024));
	if(pE->sLabels[0] == '#'){
		int nWant = atoi(pE->sLabels + 1);
		int nHave = 1;
		for(const char* p = sList; *p != '\0'; ++p) if(*p == ';') ++nHave;
		CHECK(nHave == nWant);
	}
	else{
		if(strcmp(sList, pE->sLabels) != 0)
			printf("   labels: have '%s' want '%s'\n", sList, pE->sLabels);
		CHECK(strcmp(sList, pE->sLabels) == 0);
	}

	/* UNIT_PTR_1 only where the law splits the units, UNITS otherwise */
	bool bUnitPtr = getStrEntry(id, iVar, "UNIT_PTR_1", sPtr, 256);
	CHECK(bUnitPtr == (pE->sUnits != NULL));
	if(bUnitPtr && (pE->sUnits != NULL)){
		CHECK(getStrList(id, sPtr, sList, 1024));
		if(strcmp(sList, pE->sUnits) != 0)
			printf("   units: have '%s' want '%s'\n", sList, pE->sUnits);
		CHECK(strcmp(sList, pE->sUnits) == 0);
	}
	CHECK(getStrEntry(id, iVar, "UNITS", sPtr, 256) == pE->bHasUnitsAttr);

	/* REPRESENTATION_1 names the component symbols in storage order */
	bool bRep = getStrEntry(id, iVar, "REPRESENTATION_1", sPtr, 256);
	CHECK(bRep == (pE->sRep != NULL));
	if(bRep && (pE->sRep != NULL)){
		CHECK(getStrList(id, sPtr, sList, 1024));
		if(strcmp(sList, pE->sRep) != 0)
			printf("   representation: have '%s' want '%s'\n", sList, pE->sRep);
		CHECK(strcmp(sList, pE->sRep) == 0);
	}

	/* the formalism as attributes: every required pair, none of the forbidden */
	char sPairs[512] = {'\0'};
	strncpy(sPairs, pE->sAttrs, 511);
	for(char* sPair = strtok(sPairs, "|"); sPair != NULL; sPair = strtok(NULL, "|")){
		char* sEq = strchr(sPair, '=');
		CHECK(sEq != NULL);
		if(sEq == NULL) continue;
		*sEq = '\0';
		bool bHave = getStrEntry(id, iVar, sPair, sPtr, 256);
		if(!bHave || (strcmp(sPtr, sEq + 1) != 0))
			printf("   attr %s: have '%s' want '%s'\n", sPair, bHave ? sPtr : "<absent>", sEq + 1);
		CHECK(bHave && (strcmp(sPtr, sEq + 1) == 0));
	}
	char sNames[256] = {'\0'};
	strncpy(sNames, pE->sAbsent, 255);
	for(char* sName = strtok(sNames, "|"); sName != NULL; sName = strtok(NULL, "|")){
		if(getStrEntry(id, iVar, sName, sPtr, 256))
			printf("   attr %s: present as '%s', must be absent\n", sName, sPtr);
		CHECK(!getStrEntry(id, iVar, sName, sPtr, 256));
	}

	/* global attributes, for the options that govern the global area */
	strncpy(sPairs, pE->sGlobals, 511);
	for(char* sPair = strtok(sPairs, "|"); sPair != NULL; sPair = strtok(NULL, "|")){
		char* sEq = strchr(sPair, '=');
		CHECK(sEq != NULL);
		if(sEq == NULL) continue;
		*sEq = '\0';
		bool bHave = getGlobalEntry(id, sPair, sPtr, 256);
		if(!bHave || (strcmp(sPtr, sEq + 1) != 0))
			printf("   global %s: have '%s' want '%s'\n", sPair, bHave ? sPtr : "<absent>", sEq + 1);
		CHECK(bHave && (strcmp(sPtr, sEq + 1) == 0));
	}
	strncpy(sNames, pE->sGlobalsAbsent, 255);
	for(char* sName = strtok(sNames, "|"); sName != NULL; sName = strtok(NULL, "|")){
		if(getGlobalEntry(id, sName, sPtr, 256))
			printf("   global %s: present as '%s', must be absent\n", sName, sPtr);
		CHECK(!getGlobalEntry(id, sName, sPtr, 256));
	}

	/* the values, in the slots the labels claim */
	if(pE->nVals > 0){
		long nType = 0;
		CHECK(CDFgetzVarDataType(id, iVar, &nType) == CDF_OK);
		CHECK((nType == CDF_DOUBLE)||(nType == CDF_REAL8)||(nType == CDF_FLOAT)||(nType == CDF_REAL4));
		double aRec[64] = {0.0};
		float  aRecF[64] = {0.0f};
		bool bDbl = (nType == CDF_DOUBLE)||(nType == CDF_REAL8);
		CHECK(CDFgetzVarRecordData(id, iVar, pE->iRec, bDbl ? (void*)aRec : (void*)aRecF) == CDF_OK);
		for(int i = 0; i < pE->nVals; ++i){
			double r = bDbl ? aRec[i] : (double)aRecF[i];
			if(fabs(r - pE->aVals[i]) >= pE->rTol)
				printf("   value[%d]: have %g want %g\n", i, r, pE->aVals[i]);
			CHECK(fabs(r - pE->aVals[i]) < pE->rTol);
		}
	}

	CDFcloseCDF(id);
}

int main(int argc, char** argv)
{
	if(argc < 2){
		fprintf(stderr, "Usage: TestCdf BUILD_DIR   (holding das3_cdf's ex40..ex43 output)\n");
		return 13;
	}

	size_t uExpect = sizeof(g_aExpect) / sizeof(expect_t);
	for(size_t u = 0; u < uExpect; ++u)
		checkOne(argv[1], g_aExpect + u);

	if(nErrs > 0){
		printf("ERROR: TestCdf: %d check(s) failed\n", nErrs);
		return 7;
	}
	printf("INFO: TestCdf: all composite-in-CDF checks passed\n");
	return 0;
}
