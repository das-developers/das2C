/* Copyright (C) 2015-2019 Chris Piker <chris-piker@uiowa.edu>
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
#include <ctype.h>
#include <stdbool.h>

#include "util.h"
#include "dsdf.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

/* IDL 8.3 has a 2250 character line limit.  The value below should get up to
   10 max length continuation lines */
#define IDL_STR_BUF_LIMIT 22500

static const char* g_sIdlBin = NULL;

/*****************************************************************************/
/* Set the location of the IDL binary */

const char* dsdf_setIdlBin(const char* sIdlBin)
{
	const char* sOldIdlBin = g_sIdlBin;
	g_sIdlBin = sIdlBin;
	return sOldIdlBin;
}

/*****************************************************************************/
/* DSDF Parsing */

void _removeComments(char* line)
{
	int i;
	char quoteChar = 0;
	char cc;
	int cont;

	i = 0;
	cont = 1;
	while (cont && i < strlen(line)) {
		cc = line[i];
		if (quoteChar) {
			if (cc == quoteChar) {
				quoteChar = 0;
			}
		} else {
			if (cc == '\'' || cc == '\"') {
				quoteChar = cc;
			} else if (cc == ';') {
				line[i] = '\0';
				cont = 0;
			}
		}
		i++;
	}
}

char* _stringTrim( char * string ) {
    int i;
    int isWhiteSpace;
    int cont;
    char cc;
    char * result=NULL;

    /* find first non-whitespace char */
    i=0;
    cont=1;
    while( cont && i<strlen(string)-1 ) {
        cc= string[i];
        isWhiteSpace= ( cc==' ' || cc=='\t' );
        if ( !isWhiteSpace ) {
            result= string+i;
            cont= 0;
        } else {
            i++;
        }
    }

    /* find last non-whitespace char */
    i= strlen( string ) -1;
    cont= 1;
    while ( cont && i>=0 ) {
        cc= string[i];
        isWhiteSpace= ( cc==' ' || cc=='\t' || cc=='\n' );
        if ( !isWhiteSpace ) {
            string[i+1]=0;
            cont=0;
        } else {
            i--;
        }
    }
    return result;
}

char* _stringUnquote(char * sVal)
{
	char quoteChar;
	quoteChar = sVal[0];
	if (quoteChar != '\'' && quoteChar != '"') {
		das_error(23, "DSDF string to be unquoted doesn't appear to be "
				"quoted: %s\n", sVal);
		return NULL;
	}

	if( sVal[ strlen(sVal) - 1 ] != quoteChar) {
		das_error(23, "DSDF string missing closing quote: %s\n", sVal);
		return NULL;
	}
	sVal[ strlen(sVal) - 1 ] = '\0';

	return sVal + 1;
}


/* ************************************************************************* */
/* Main Parser Function */

DasDesc* dsdf_parse(const char* sFileName)
{
	char sLine[IDL_STR_BUF_LIMIT] = {'\0'}; 
	
	size_t uLine = 0;
	char* pWrite = NULL;
	size_t uRead = 0;
	size_t uSpace = 22500;
	
	char* pEquals;
	char* sKey;
	char* sVal;
	FILE* pFile;
	DasDesc* pDsdf = NULL;

	if( (pFile = fopen( sFileName, "r" )) == NULL){
		das_error(23, "Error opening %s", sFileName);
		return NULL;
	}
	 
	pDsdf = new_Descriptor();
	pDsdf->bLooseParsing = true;  /* The only place I know of that uses this. -cwp */
	bool bDone = false;
	while(!bDone){
		
		/* Handle IDL continuation character, which is a '$' */
		pWrite = sLine;
		uRead = 0;
		uSpace = IDL_STR_BUF_LIMIT;
		while(uSpace > 2){
			fgets(pWrite, uSpace, pFile);
			uLine += 1;
			uRead = strlen(pWrite);
			uSpace -= uRead;
			
			if(uRead == 0){
				bDone = true;
				break;
			}
			
			/* if continued */
			pWrite += uRead - 1;
			
			/* if Continuation line */
			if(uRead == 1) break;
			if((uRead > 1) && (*(pWrite - 1) != '$')) break;
			
			pWrite -= 1;
			*pWrite = '\0';
			uSpace += 1;
		}
		
		_removeComments(sLine);
		
		/* split at =, should be no more continuation lines at this point, lines
		 * with no = are ignored */
		if( (pEquals = strchr( sLine, '=' )) !=NULL ){ 
			*pEquals = '\0';
			
			sKey = _stringTrim(sLine);
			sVal = _stringTrim(pEquals+1);
			
			if(sVal[0] == '\'' || sVal[0] == '\"' ){       
				sVal = _stringUnquote(sVal);
			}
			DasDesc_set(pDsdf, "String", sKey, sVal );
		}
		else{
			size_t uLen = strlen(sLine); 
			for(size_t u = 0; u<uLen; u++){
				if( ! isspace(sLine[u]) ){
					das_error(23, "Syntax error in %s at line %zu", sFileName, uLine);
					return NULL;
				}
			}
		}

		memset(sLine, '\0', IDL_STR_BUF_LIMIT - uSpace);
    }
   fclose( pFile );
	
	return pDsdf;
}

/* ************************************************************************* */
/* Parsing DSDF values */

/* Run an IDL subprocess to get the length of the array, slows down the
   reader by quite a bit, but allows old stuff to be used un-altered */
double* dsdf_valToArrayIDL(const char* sArrayDef, size_t* pLen)
{
	size_t uWrote = 0;
	char sBuf[8096] = {'\0'};
	FILE* pPipe = NULL;
	double* pVals = NULL;
	const char* pRead = NULL;
	
	/* build the command string */
	uWrote = snprintf(sBuf, 1023, "%s -quiet -e \"a = %s & print, size(a, "
			"/N_ELEMENTS) & print, a\"", g_sIdlBin, sArrayDef);

	/* open pipe to IDL */
	if( (pPipe = popen(sBuf, "r")) == NULL ) {
		das_error(23, "Could not open IDL process via %s\n", sBuf);
		return NULL;
	}

	/* The first line should be the number of elements in the array */
	memset(sBuf, 0, uWrote);
	fgets(sBuf, 8095, pPipe);
	if (sscanf(sBuf, " %zu ", pLen) != 1) {
		das_error(23, "Couldn't get the number of elements in %s", sBuf);
		pclose(pPipe);
		return NULL;
	}

	if ((pVals = (double*) calloc(*pLen, sizeof (double))) == NULL) {
		das_error(23, "Couldn't allocate an array of %d doubles", *pLen);
		return NULL;
	}

	size_t uTag = 0;
	while(fgets(sBuf, 8095, pPipe) != NULL) {
		pRead = sBuf;
		while (isspace(*pRead) && *pRead != '\n' && *pRead != '0') pRead++;
		if( sscanf(pRead, "%lf", pVals + uTag) != 1){
			das_error(23, "Error next value at %s ", pRead);
			return NULL;
		}
		uTag++;
		if(uTag >= *pLen) break;
	}
	
	if(uTag != *pLen){
		das_error(23, "Only read %zu of %zu yTags", uTag, *pLen);
		return NULL;
	}
	
	/* consume all the output so IDL doesn't complain when the stream is closed */
	while(!feof(pPipe)) {
		fgets(sBuf, 8095, pPipe);
   }

   pclose(pPipe);

	return pVals;
}

/* ************************************************************************* */
/* Parse Text String into a double array, invoke IDL if needed */

double* dsdf_valToArray(const char* sArray, size_t* pLen) 
{
	size_t uStrLen = strlen(sArray);
	bool bIsArray = false; 
	size_t u = 0;
	char c = '\0';
	bool bInNum = false;
	double* pVals = NULL;
	const char* p = NULL;
	double* pWrite = NULL;
	
	/* (1) See if this is an array and (2) get it's length */
	bIsArray = true;
	for(u = 0; u < uStrLen; u++) {
		c = sArray[u];
		
		if(isdigit(c) || c == 'E' || c == 'e' || c == '+' || c == '-' || c == '.'){
			if(!bInNum) *pLen = *pLen + 1;
			bInNum = true;
		}
		else{
			bInNum = false;
			/* Check for only other legal things I know how to deal with */
			if( ! (isspace(c) || c == ',' || c == '[' || c == ']') ){
				bIsArray = false;
				break;
			}
		}
	}
	
	/* Doesn't look like an array I can parse, let IDL take a crack at it */
	if(!bIsArray) 
		return dsdf_valToArrayIDL(sArray, pLen);
	
	/* Okay, it's up to me now */
	if(*pLen == 0){
		das_error(23, "Array has no members");
		return NULL;
	}

	if ((pVals = (double*) calloc(*pLen, sizeof (double))) == NULL) {
		das_error(23, "Couldn't allocate an array of %d doubles", *pLen);
		return NULL;
	}
	
	bInNum = false;
	pWrite = pVals;
	for(p = sArray; *p != '\0'; p++){
		c = *p;
		
		if(isdigit(c) || c == 'E' || c == 'e' || c == '+' || c == '-' || c == '.'){
			if(!bInNum){
				if( sscanf(p, "%lf", pWrite) != 1){
					das_error(23, "Couldn't read 1st value at: %s", p);
					return NULL;
				}
				pWrite++;
			}
			bInNum = true;
		}
		else{
			bInNum = false;
		}
	}
	
	return pVals;
}

bool _allSpace(const char* str){
	while(*str != '\0'){
		if(!isspace(*str)) return false;
		str++;
	}
	return true;
}

/* Straight outta the man page... */
static int _cmpstringp(const void *p1, const void *p2)
{
	/* The actual arguments to this function are "pointers to pointers to 
	   char", but strcmp(3) arguments are "pointers to char", hence the
		following cast plus dereference */

	return strcmp(* (char * const *) p1, * (char * const *) p2);
}

char* dsdf_valToNormParam(
	const char* sRawParam, char* sNormParam, size_t uNormLen
){
	int nLen = strlen(sRawParam);
	char* sBuf = (char*)calloc(nLen + 1, sizeof(char));
	memcpy(sBuf, sRawParam, nLen+1);
	
	if((uNormLen < 9)||(uNormLen < strlen(sRawParam))) {
		das_error(23, "Output buffer sNormParam is too small");
		return NULL;
	}
	
	memset(sNormParam, '\0', uNormLen);
	
	if( sNormParam == NULL || uNormLen < 1){
		das_error(23, "Bad input arguments in dsdf_valToNormParam");
		return NULL;
	}
	
	if((sRawParam == NULL) || _allSpace(sRawParam)){
		strncpy(sNormParam, "_noparam", uNormLen-1);
		return sNormParam;
	}
	
	/* Count the tokens */
	bool bInWord = false;
	int i = 0, nTok = 0;
	for(i = 0; i<nLen; i++){
		if(bInWord){
			if(isspace(sBuf[i]))
				bInWord = false;
		}
		else{
			if(!isspace(sBuf[i])){
				bInWord = true;
				nTok++;
			}
		}
	}
	
	char** lsTok = (char**)calloc(nTok, sizeof(char*));

	int j = 0;
	bInWord = false;
	for(i = 0; i<nLen; i++){
		if(bInWord){
			if(isspace(sBuf[i]))
				bInWord = false;
		}
		else{
			if(!isspace(sBuf[i])){
				lsTok[j] = sBuf + i;
				bInWord = true;
				j++;
			}
		}
		/* No matter the setting, spaces become nulls */
		if(isspace(sBuf[i])) sBuf[i] = '\0';
	}
	
	/* Apply the merge rule for things like -d output_dir, works because
	   all pointers go to single buffer */
	int k = 0;
	for(j = 0; j < nTok-1; j++){		
		if((lsTok[j][0] == '-') && (lsTok[j+1][0] != '-')){
			lsTok[j][ strlen(lsTok[j]) ] = '_';
			
			for(k = j+1; k < nTok-1; k++)  /* Merge down... */
				lsTok[k] = lsTok[k+1];
			
			nTok--;
		}
	}
	
	/* Sort the array */
	qsort(lsTok, nTok, sizeof(char*), _cmpstringp);
	
	/* Join */
	char* pWrite = sNormParam;
	for(j = 0; j < nTok; j++){
		nLen = strlen(lsTok[j]);
		strncpy(pWrite, lsTok[j], nLen);
		pWrite += nLen;
		
		if(j < nTok - 1) *pWrite = '_';
		else *pWrite = '\0';
		
		pWrite++;
	}
	free(sBuf);
	free(lsTok);
	return sNormParam;
}
