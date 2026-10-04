/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5.1
 *
 * This file is part of das2C, the Core Das2 C Library.
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

/** @file cdfmodel.h  The CDF file model shared by the from_cdf programs.
 *
 * Opens an ISTP style CDF, inventories its variables and classifies them
 * into datasets by DEPEND chain.  Nothing here knows which das stream
 * version is written; the programs build that from the model.
 */

#ifndef _das_tools_cdfmodel_h_
#define _das_tools_cdfmodel_h_

#include <stdbool.h>
#include <das3/core.h>
#include <cdf.h>

/* Exit code shared by the from_cdf programs */
#define PERR (DASERR_MAX + 10)

#define MAX_DATA_VARS 32     /* variables selectable on one command line */

/* ************************************************************************* */
/* CDF status handling, requires a local nCdfStatus.  Errors are logged,
   warnings and informational statuses are logged and let through. */

bool cdf_okayish(CDFstatus iStatus);

#define CDF_MAD( SOME_CDF_FUNC ) ( ((nCdfStatus = (SOME_CDF_FUNC) ) != CDF_OK) && (!cdf_okayish(nCdfStatus)) )

/* ************************************************************************* */
/* File inventory: one record per zVariable, filled from the CDF and then
   annotated by the classifier.  Both the dry run listing and the stream
   builder work from this. */

#define CVAR_NAME_SZ (CDF_VAR_NAME_LEN256 + 1)
#define MAX_CDF_VARS 256
#define MAX_CDF_DS   32
#define MAX_COMPS    16      /* labels kept per component set */
#define ADVICE_SZ    4096

typedef enum var_role {
	ROLE_UNK = 0,
	ROLE_TIME,      /* a time base */
	ROLE_DATA,      /* streams as a <data> (or annotation <coord>) */
	ROLE_COORD,     /* a DEPEND_N table of a streamed variable */
	ROLE_OFFSET,    /* an OFFSET_OF table for a time base */
	ROLE_LABEL,     /* a LABL_PTR_N target */
	ROLE_DELTA,     /* a DELTA_PLUS/MINUS_VAR target */
	ROLE_IGNORE
} var_role_e;

typedef struct cdf_var {
	char sName[CVAR_NAME_SZ];
	long nVarNum;
	long nType;
	long nElems;                 /* chars per value for CDF_CHAR */
	long nDims;                  /* not counting the record dimension */
	long aDimSz[CDF_MAX_DIMS];
	long bRecVary;
	long nRecs;

	/* ISTP attributes the heuristics read, empty when absent */
	char sVarType[32];
	char asDepend[VARIDX_MAX][CVAR_NAME_SZ];    /* [0] = DEPEND_0 */
	char asLablPtr[VARIDX_MAX][CVAR_NAME_SZ];   /* [1] = LABL_PTR_1 */
	char sOffsetOf[CVAR_NAME_SZ];
	char sDeltaPlus[CVAR_NAME_SZ];
	char sDeltaMinus[CVAR_NAME_SZ];
	char sDictKey[128];
	char sUnits[64];
	char sFrame[64];
	char sFrameAttr[64];         /* which attribute supplied sFrame */

	/* classification */
	var_role_e role;
	char sWhy[160];              /* why ignored, or how used */
	bool bTimeDep;               /* DEPEND_0 is a time base */
	bool bSelected;              /* streams */
	int  iTime;                  /* index of the time base, -1 */
	int  iUsedBy;                /* support: first variable served, -1 */
	int  iDs;                    /* dataset index, -1 */

	/* structure of a selected variable */
	int  nExtRank;               /* record index + DEPEND_N dims */
	int  aIsInternal[CDF_MAX_DIMS];  /* per CDF dim: 1 = component axis */
	int  nComps;                 /* product of internal dims, 1 for a scalar */
	char sKind[32];              /* scalar, vector, complex, bundle, matrix */
	char sSystem[16];            /* cartesian, spherical, ... rectangular, polar */
	char sSyms[64];              /* "x,y,z" */
	char aLabels[MAX_COMPS][32]; /* component labels as read */
	int  nLabels;
	ubyte uSys;                  /* DAS_VSYS_* when sKind is vector */
	ubyte aDirs[MAX_COMPS];      /* canonical direction of each component */
	int  iCompDim;               /* CDF dim of the component axis, -1 if none */
	int  iCompSel;               /* selected component on it, -1 = all */
	char sCompSel[32];           /* how the user spelled it */
	bool bFolded;                /* joined a higher rank dataset, degenerate on its extra indices */
	bool bAnnot;                 /* folded as a coordinate annotation, not data */
} cdf_var_t;

/* The hyperget window that reads one record varying variable: whole dims,
   or one component of the component axis */
typedef struct rec_reader {
	int     iVar;
	long    nElemsPerRec;
	bool    bTime;               /* EPOCH values convert to TT2000 on the way in */
	long    nDims;               /* the hyperget window, fixed by the first file */
	long    aStart[CDF_MAX_DIMS];
	long    aCount[CDF_MAX_DIMS];
} rec_reader_t;

typedef struct cdf_ds {
	char sName[64];
	char sGroup[64];
	int  nRank;
	int  iTime;
	char sChain[VARIDX_MAX * CVAR_NAME_SZ];  /* DEPEND names joined, the key */
	int  aMembers[MAX_CDF_VARS];
	int  nMembers;

	/* read windows for the record varying members, in build order */
	rec_reader_t aRead[MAX_CDF_VARS];
	int    nRead;
} cdf_ds_t;

typedef struct cdf_file {
	CDFid id;
	long nMajority;              /* ROW_MAJOR or COLUMN_MAJOR, how records are stored */
	char sPath[DURI_MAX_PATH];
	char sSource[128];           /* Logical_source, or a short TITLE, or "" */
	cdf_var_t aVars[MAX_CDF_VARS];
	int nVars;
	cdf_ds_t aDs[MAX_CDF_DS];
	int nDs;
	char sAdvice[ADVICE_SZ];
} cdf_file_t;

/* What the user asked for, as the classifier needs it.  Strings may be
   empty but not NULL. */
typedef struct cdf_select {
	const char*  sCoordMap;        /* --coord, "COORD:VAR,COORD:VAR" */
	const char*  sDefVars;         /* --def-vars, comma separated */
	const char** asVars;           /* named variables, VAR or VAR.COMPONENT */
	int          nVars;
	const das_range* pTimeRng;     /* the time range, NULL when unbounded */
	bool         bDataOnly;        /* default selection: VAR_TYPE data only, no
	                                  support variables and so no folding */
} cdf_select_t;

/* ************************************************************************* */
/* The property map: CDF attribute name to das key, "FROM:TO,FROM:TO".  The
   built-in table is the inverse of das3_cdf's; the user's entries are
   consulted first.  Parse once before any file is opened. */

int cdf_parsePropMap(const char* sMap);

/* ************************************************************************* */
/* Opening and classifying */

/* Open a CDF, inventory and classify it.  Returns DAS_OKAY with the file
   open, or an error with it closed. */
int cdf_openAndClassify(cdf_file_t* pFile, const char* sPath, const cdf_select_t* pSel);

/* The dry run listing of a classified file, to standard output */
void cdf_listFile(cdf_file_t* pFile, const cdf_select_t* pSel);

/* The variables a user can name, one line each with their components in
   the VAR.COMPONENT spelling and a one line description.  Classify with
   nothing named first so every time dependent variable is selected.
   Variables with more than nMaxRank coordinates go in a second section
   headed sDeeperTitle; pass VARIDX_MAX and NULL for no split. */
void cdf_listVars(cdf_file_t* pFile, int nMaxRank, const char* sDeeperTitle);

/* Structure signature: what a later file must match to reuse the datasets */
void cdf_structSig(const cdf_file_t* pFile, char* sBuf, size_t uLen);

/* Index of a variable by name, -1 if absent */
int cdf_varIndex(const cdf_file_t* pFile, const char* sName);

/* ************************************************************************* */
/* Per variable translation to das terms */

bool cdf_isTimeType(long nType);

/* das value type for a CDF type, vtUnknown for text and EPOCH16 */
das_val_type cdf_valType(long nType);

/* The das3 semantic word: datetime, real or int */
const char* cdf_semantic(const cdf_var_t* pV);

/* The fill for a variable's values: FILLVAL when present and of the same
   type, else the das default for the type.  Time bases are always TT2000
   longs.  pBuf must hold 16 bytes; the return may point into it. */
const ubyte* cdf_varFill(cdf_file_t* pFile, const cdf_var_t* pV, ubyte* pBuf);

/* Units for a variable, dimensionless for placeholders and parse failures */
das_units cdf_varUnits(const cdf_var_t* pV);

/* Is a non record varying rank-1 numeric table an arithmetic sequence? */
bool cdf_isSequence(cdf_file_t* pFile, const cdf_var_t* pV, double* pMin, double* pStep);

/* A variable attribute as a string.  Numeric attributes are formatted; a
   missing attribute leaves sBuf empty and returns false. */
bool cdf_varAttrStr(CDFid id, long iVar, const char* sAttr, char* sBuf, size_t uLen);

/* Attribute names the classifier consumed, which are not repeated as
   properties */
bool cdf_attrConsumed(const cdf_var_t* pV, const char* sAttr);

/* das property name for a CDF variable attribute: the user's map, then
   the built-in table, then the name as-is */
const char* cdf_propName(const char* sAttr);

/* Copy a variable's attributes onto a descriptor as properties, skipping
   the ones the classifier consumed */
int cdf_addVarProps(cdf_file_t* pFile, const cdf_var_t* pV, DasDesc* pDest);

/* Global attributes as properties on a descriptor, multi-entry attributes
   as string arrays */
int cdf_addGlobalProps(cdf_file_t* pFile, DasDesc* pDest);

/* ************************************************************************* */
/* Reading records */

/* Set a reader's window for a variable: all of it, or the one selected
   component.  Returns the elements read per record. */
long cdf_initReader(const cdf_file_t* pFile, int iVar, rec_reader_t* pR);

/* Read a block of records of one variable into a malloc'ed buffer through
   the reader's window.  Records come back row major (last dim fastest)
   whatever the file's majority.  NULL on failure, after logging. */
ubyte* cdf_readBlock(
	cdf_file_t* pFile, long nVarNum, const rec_reader_t* pR, long nRec0, long nRecs, size_t uElemSz
);

#endif /* _das_tools_cdfmodel_h_ */
