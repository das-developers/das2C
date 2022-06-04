#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include <das2/core.h>
#include "cli.h"

/** Strings to assist with output operations */
#define DAS_OUT_INTERVAL  ".int."
#define DAS_OUT_SWITCH   ".out."

/* Use "hi-band" flags to avoid conflicts with the header file definitions */
#define DAS_OUT_ENABLE   0x0100 
#define DAS_OUT_DISABLE  0x0200

#define CLI_ERROR 45
#define USAGE_ERROR 46

/* ************************************************************************* */
/* Globals (hide your eyes) */

static int g_nLogLevel = DAS_LL_INFO;
static const char* g_sProgName = NULL;


/***************************************************************************/
/* Utilities */

bool _intConv(const char* sVal, int* pRet){
	size_t i, len;
	int nBase = 10;
	long int lRet;
	char* endptr;

	if((sVal == NULL)||(pRet == NULL)){
		fprintf(stderr, "USAGE ERROR: Null pointer to _intConv\n");
		exit(USAGE_ERROR);
	}
		
	len = strlen(sVal);
	
	/* check for hex, don't use strtol's auto-base as leading zero's cause
	   a switch to octal */
	for(i = 0; i<len; i++){
		if((sVal[i] != '0')&&isalnum(sVal[i])) break;
		
		if((sVal[i] == '0')&&(i<(len-1))&&
			((sVal[i+1] == 'x')||(sVal[i+1] == 'X')) ){
			nBase = 16;
			break;
		}
	}	
	
	errno = 0;
	lRet = strtol(sVal, &endptr, nBase);
	
	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ) return false;
	
	if(endptr == sVal) return false;
	
	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;
	
	*pRet = (int)lRet;
	return true;
}

const DasSelector* _selOrExit(const DasSelector* pSels, const char* sKey)
{
	const DasSelector* pSel = pSels;
	
	while(pSel->sKey != NULL){
		if(strcmp(pSel->sKey, sKey) == 0) return pSel;
		pSel++;
	}
	
	fprintf(stderr, "USAGE ERROR: Selector '%s' was not defined.\n", sKey);
	exit(USAGE_ERROR);
	
	return NULL;
}

const DasSelector* _findSelector(const DasSelector* pSels, const char* sKey)
{
	const DasSelector* pSel = pSels;
	
	while(pSel->sKey != NULL){
		if(strcmp(pSel->sKey, sKey) == 0) return pSel;
		pSel++;
	}
	
	return NULL;
}

const DasOutput* _outOrExit(const DasOutput* pOuts, const char* sKey)
{
	const DasOutput* pOut = pOuts;
	
	while(pOut->sKey != NULL){
		if(strcmp(pOut->sKey, sKey) == 0) return pOut;
		pOut++;
	}
	
	fprintf(stderr, "USAGE ERROR: Output '%s' was not defined.\n", sKey);
	exit(USAGE_ERROR);
	
	return NULL;
}

const DasOutput* _findOutput(const DasOutput* pOuts, const char* sKey)
{
	const DasOutput* pOut = pOuts;
	
	while(pOut->sKey != NULL){
		if(strcmp(pOut->sKey, sKey) == 0) return pOut;
		pOut++;
	}
		
	return NULL;
}

bool _hasOperator(const char* sString){
	char sCheck[80] = {'\0'};
	
	strncpy(sCheck, sString, 79);
	for(int i = 0; i < strlen(sCheck); i++) sCheck[i] = tolower(sCheck[i]);
	
	if(strstr(sCheck, OP_EQ)) return true;
	if(strstr(sCheck, OP_NE)) return true;
	if(strstr(sCheck, OP_LT)) return true;
	if(strstr(sCheck, OP_GT)) return true;
	if(strstr(sCheck, OP_LE)) return true;
	if(strstr(sCheck, OP_GE)) return true;
	if(strstr(sCheck, ".beg.")) return true;
	if(strstr(sCheck, ".end.")) return true;
	if(strstr(sCheck, DAS_OUT_INTERVAL)) return true;
	if(strstr(sCheck, DAS_OUT_SWITCH)) return true;
	
	return false;
}

/***************************************************************************/
/* Final initializations for the selectors */

const char* g_allBounds[] = { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, NULL};
const char* g_boolBounds[] = { OP_EQ, NULL };

void initSelValues(DasSelector* pSels)
{
	
	DasSelector* pSel = pSels;
	while(pSel->sKey != NULL){
		
		if(pSel->nFmt & ENUM){
			pSel->psValues = (char**)calloc(2, sizeof(char*));
		}
		else{
			
			/* If no specific set of allow operations were allowed, then allow 
			   all for non boolean selectors */
			if(pSel->psBounds == NULL){
				if(pSel->nFmt != bool_t)
					pSel->psBounds = g_allBounds;
				else
					pSel->psBounds = g_boolBounds;
			}
	
			size_t uOps = 0;
			while(pSel->psBounds[uOps] != NULL) uOps++;
	
			pSel->psValues = (char**) calloc(uOps + 1, sizeof(char*));
			/* Relies on the fact that NULL actually == 0 */	
			
		}
		pSel++;
	}
}

/* Final initialization for outputs, turn everything on by default */

void initOutMaybeEnable(DasOutput* pOuts)
{
	DasOutput* pOut = pOuts;
	while(pOut->sKey != NULL){
		if(! (pOut->nOpts & DAS_OUT_DISABLE))
			pOut->nOpts |= DAS_OUT_ENABLE;
		pOut++;
	}
}


/***************************************************************************/
/* Check Library User's setup */

/* Might choose to expose this function at a later date */
void checkSelectors(const DasSelector* pSels)
{
	
	/* Should always define at least one selector */
	int nNonEmpty = 0;
	int nComparitors = 0;
		
	for(const DasSelector* pSelCk = pSels; pSelCk->sKey != NULL; pSelCk++){
		nNonEmpty++;
		
		if(pSelCk->nFlags & ENUM){
			
			/* Make sure the type is string_t */
			if(pSelCk->nFmt != string_t){
				fprintf(stderr, "USAGE ERROR: Enumeration '%s' should be a"
						  " string_t type selector\n", pSelCk->sKey);
				exit(USAGE_ERROR);
			}
		
			nNonEmpty = 0;
			for(int i = 0; pSelCk->psBounds[i] != NULL; i++) nNonEmpty++;
		
			if(! nNonEmpty){
				fprintf(stderr, "USAGE ERROR: Enumeration %s has no values "
						  "defined\n", pSelCk->sKey);
				exit(USAGE_ERROR);
			}
		}
		else{
			
			if(pSelCk->psBounds != NULL){
				
				nComparitors = 0;
				for(int i = 0; pSelCk->psBounds[i] != NULL; i++){ 
					nComparitors++;
					const char* pOp = pSelCk->psBounds[i];
					
					if( (strcasecmp(pOp, OP_EQ) != 0) &&
 						 (strcasecmp(pOp, OP_NE) != 0) &&
						 (strcasecmp(pOp, OP_LT) != 0) &&
						 (strcasecmp(pOp, OP_GT) != 0) &&
						 (strcasecmp(pOp, OP_LE) != 0) &&
						 (strcasecmp(pOp, OP_GE) != 0)  
						){
						
						fprintf(stderr, "USAGE ERROR: Unknown comparitor '%s' in "
								  "the allowed comparisons array for selector '%s'\n",
								  pOp, pSelCk->sKey);
						exit(USAGE_ERROR);
					}
				}
				
				if(! nComparitors){
					fprintf(stderr, "USAGE ERROR: Non-null allowed comparison "
							  "array for selector %s has no comparisons defined.\n",
							   pSelCk->sKey);
					exit(USAGE_ERROR);
				}	
			} /* If bounds array was defined */
			
		}
	}
	
	if(! nNonEmpty ){
		fprintf(stderr, "USAGE ERROR: No data selectors defined!\n");
		exit(USAGE_ERROR);
	}
}
	
void checkOutputs(DasOutput* pOuts)
{
				
	int nNonEmpty = 0;   /* Should always define at least one output */
	int nDependent = 0;  /* Should always define at least one dependent value */
	
	for(DasOutput* pOutCk = pOuts; pOutCk->sKey != NULL; pOutCk++){
		nNonEmpty++;
		
		if(pOutCk->psDepends != NULL){
			nDependent++;
			
			int nDependsOn = 0; /* Dependency array should contan something */
			
			for(int i = 0; pOutCk->psDepends[i] != NULL; i++){
				nDependsOn++;  /* Make sure depends array has something in it */
				
				/* Now make sure what it depends on is valid */
				bool bFoundDep = false;
				for(DasOutput* pDep = pOuts; pDep->sKey != NULL; pDep++){
					if(strcmp(pOutCk->psDepends[i], pDep->sKey) == 0){
						bFoundDep = true;
						if(pDep->psDepends != NULL){
							fprintf(stderr, "USAGE ERROR: Output '%s' depends on '%s' "
									  " 'but '%s' is not an independent variable.\n",
									  pOutCk->sKey, pDep->sKey, pDep->sKey);
							exit(USAGE_ERROR);
						}
					}
				}
				
				if(!bFoundDep){
					fprintf(stderr, "USGAGE ERROR: Undefined dependency '%s' for"
							  " output '%s'\n",  pOutCk->psDepends[i], pOutCk->sKey);
					exit(USAGE_ERROR);
				}
					
			}
			if(nDependsOn == 0){
				fprintf(stderr, "USAGE ERROR: Output '%s' dependency array is"
						  " not-null, but contains no entries\n", pOutCk->sKey);
				exit(USAGE_ERROR);
			}
		}  /* End dependent variable checking */
	}
		
	if(! nNonEmpty ){
		fprintf(stderr, "USAGE ERROR: No outputs defined!\n");
		exit(USAGE_ERROR);
	}	
	
	if( ! nDependent){
		fprintf(stderr, "USAGE ERROR: No dependent output variables defined\n");
		exit(USAGE_ERROR);
	}

}

/* ************************************************************************* */
/* Helpers for help printing */

const char* _metaVar(enum das_selfmt nFmt){
	switch(nFmt){
	case bool_t:   return "BOOL";
	case int_t:    return "INTEGER";
	case real_t:   return "REAL";
	case string_t: return "STRING";
	case timept_t: return "DATETIME";
	}
	return NULL;
}

/* Alloc's memory, client is responsible for cleaning up the memory if needed */
char* _getEnumStr(const char** psBounds){
	size_t nLen = 1;
	size_t iWrite = 0;
	const char* sStr;
	int i = 0;
	while( (sStr = psBounds[i]) != NULL){
		nLen += strlen(sStr);
		if(i > 0) nLen += 1;
		i += 1;
	}
	
	char* sEnum = calloc(nLen, sizeof(char));
	
	i = 0;
	iWrite = 0;
	while( (sStr = psBounds[i]) != NULL){
		
		if(i > 0){
			sEnum[iWrite] = ','; iWrite++;
			sEnum[iWrite] = ' '; iWrite++;
		}
		
		strcpy(sEnum + iWrite, psBounds[i]);
		iWrite += strlen(sStr);
		i += 1;
	}
	
	return sEnum;
}


/* Wrap text to width, indent with indent string.  Literal new-line 
   characters are retained, all other whitespace is ignored.  sIndent may
	be NULL
*/
void wrapf(FILE* fOut, int nWidth, const char* sIndent, const char* sTxt)
{
	char sNewLine[80] = {'\0'};
	if(sIndent != NULL) snprintf(sNewLine, 79, "\n%s", sIndent);	
	else sNewLine[0] = '\n';
	
	int nIndent = strlen(sNewLine) - 1;
	
	/* Word loop */
	int nCol = 1;  /* nCol is a 1 based offset, not 0 based */
	int nWord = 0;
	const char* pBeg = sTxt;
	const char* pEnd = NULL;
	while(*pBeg != '\0'){
		
		/* There may be whitespace to skip, advance to beginning of next word.  
		   newline characters are always emitted as soon as they are seen but
			are otherwise skipped. */
		
		while(isspace(*pBeg) && *pBeg != '\0'){ 
			if(*pBeg == '\n'){ 
				fputs("\n", fOut);
				nCol = 1;
			}
			++pBeg;
		}
		if(*pBeg == '\0') break;
		
		/* At a non-space character, find end of this word */
		pEnd = pBeg;
		while( !isspace(*pEnd) && *pEnd != '\0') pEnd++;
		nWord = pEnd - pBeg;
		
		
		/* handle preceeding space */
		
		/* Do we need to indent first? */
		if(nCol == 1){
			if(nIndent > 0){
				fputs(sIndent, fOut);
				nCol += nIndent;
			}
		}
		else{
			/* Do we need a line break? */
			if((nWord + nCol > nWidth)&&(nCol != (nIndent + 1))){
				fputs(sNewLine, fOut);
				nCol = nIndent + 1;
			}
			else{
				/* Nope, just a space character */
				putc(' ', fOut);
				nCol += 1;	
			}
		}
			
		/* Okay now really print it */
		fwrite(pBeg, sizeof(char), nWord, fOut);
		nCol += nWord;
		
		pBeg = pEnd;
	}
}


/* Alloc's memory, client is responsible for cleaning up the memory if needed */
char* _getSelOpsStr(const DasSelector* pSel){
	
	size_t nLen = 1;
	size_t iWrite = 0;/** Equivalent to das_get_seltime but uses the new das_time_ type from time.h */
	const char* sStr;
	int i = 0;
	while( (sStr = pSel->psBounds[i]) != NULL){
		
		if(pSel->nFlags & XLATE_GE_LT){
			if(strcasecmp(OP_GE, sStr) == 0) sStr = ".beg.";
			if(strcasecmp(OP_LT, sStr) == 0) sStr = ".end.";
		}
					
		nLen += strlen(sStr);
		if(i > 0) nLen += 1;
		i += 1;
	}
	
	char* sOps = calloc(nLen, sizeof(char));
	
	i = 0;
	iWrite = 0;
	while( (sStr = pSel->psBounds[i]) != NULL){
		
		if(i > 0){
			sOps[iWrite] = ' ';
			iWrite++;
		}
		
		if(pSel->nFlags & XLATE_GE_LT){
			if(strcasecmp(OP_GE, sStr) == 0) sStr = ".beg.";
			if(strcasecmp(OP_LT, sStr) == 0) sStr = ".end.";
		}

		strcpy(sOps + iWrite, sStr);
		iWrite += strlen(sStr);
		i += 1;
	}
	
	return sOps;
}

void _mkVsStr(const DasOutput* pOut, char* sVersus, size_t uLen)
{	
	memset(sVersus, 0, uLen);
	size_t u = 0;
	const char** psDepend = pOut->psDepends;
	
	/* Find out the string length */
	while(*psDepend != NULL){
		
		if(u > 0){
			if(u + strlen(" and ") > (uLen - 1))
				break;
			strcpy(sVersus + u, " and ");
			u += strlen(" and ");
		}
		
		if(u + strlen(*psDepend) > (uLen - 1))
			break;
		
		strcpy(sVersus + u, *psDepend);
		u += strlen(*psDepend);
		
		psDepend++;
	}
}

void _mkOutOptStr(const DasOutput* pOut, char* sOutOpts, size_t uLen)
{
	if(uLen < 40){
		fprintf(stderr, "dude, fixme\n");
		exit(47);
	}
	
	memset(sOutOpts, 0, uLen);
	size_t u = 0;
	
	if(!(pOut->nOpts & OPTIONAL) && !(pOut->nOpts & INTERVAL))
		return;
	
	sOutOpts[u] = '('; u++;
	
	if(pOut->nOpts & OPTIONAL){
		strcpy(sOutOpts + u, "optional");
		u += strlen("optional");
	}
	
	if(pOut->nOpts & INTERVAL){
		if(u>1){ 
			strcpy(sOutOpts + u, ", ");
			u += 2;
		}
		strcpy(sOutOpts + u, "variable-resolution");
		u += strlen("variable-resolution");
	}
	sOutOpts[u] = ')';
}

/* ************************************************************************* */
/* Help printer function */
	
void printHelp(const char* sBasename, const DasSelector* pSels,
               const DasOutput* pOuts, const char* sDesc, const char* sFooter)
{
	bool bHasInterval = false;
	char sOutOpts[80] = {'\0'};
	char sVersus[80] = {'\n'};
	
	/* Find out if this program has selectable resolution, if so add in the
	   resolution hint */
	const DasOutput* pOut = pOuts;
	while(pOut->sKey != NULL){
		if(pOut->nOpts & INTERVAL){
			bHasInterval = true;
			break;
		}
		pOut++;
	}

	
	fprintf(stderr,
"%s - A Das 2.1 though Das 2.3 compatible reader\n"
"\n"
"USAGE\n"
"   %s --help\n"
"   %s KEY.OP.VAL KEY.OP.VAL KEY.OP.VAL ...\n"
"   %s --das2times=SEL START STOP KEY.OP.VAL ...\n", 
	sBasename, sBasename, sBasename, sBasename);
	
	if(bHasInterval)
		fprintf(stderr, 		
"   %s --das2times=SEL --das2int=OUT INTERVAL START STOP KEY.OP.VAL ...\n",
		sBasename);
	
	fputs("\n", stderr);
					
	if(sDesc != NULL){
		fputs("DESCRIPTION\n", stderr);
		wrapf(stderr, 80, "   ", sDesc);
		fputs("\n", stderr);
	}
	
	/* Describe the outputs of the reader */
	fprintf(stderr, "\n   Output Values\n");
	fprintf(stderr,   "   -------------\n");
	
	/* Loop through finding the dependent variables, these go first */
	pOut = pOuts;
	while(pOut->sKey != NULL){
		
		if(pOut->psDepends == NULL){
			pOut++;
			continue;
		}
		
		_mkVsStr(pOut, sVersus, 80);
		_mkOutOptStr(pOut, sOutOpts, 80);
		
		if(pOut->sUnits != NULL) 
			fprintf(stderr, "   %s (%s) vs. %s %s\n", pOut->sKey, pOut->sUnits,
					  sVersus, sOutOpts);
		else
			fprintf(stderr, "   %s vs. %s %s\n", pOut->sKey, sVersus, sOutOpts);
		
		if(pOut->sSummary != NULL){
			wrapf(stderr, 80, "      ", pOut->sSummary);
			fputs("\n\n", stderr);
		}
		else
			fprintf(stderr, "      (no summary)\n\n");
		
		pOut++;
	}
	
	/* Now do the independent varables */
	pOut = pOuts;
	while(pOut->sKey != NULL){
		
		if(pOut->psDepends != NULL){
			pOut++;
			continue;
		}
		
		_mkOutOptStr(pOut, sOutOpts, 80);
		
		if(pOut->sUnits != NULL)
			fprintf(stderr, "   %s (%s) %s\n", pOut->sKey, pOut->sUnits, sOutOpts);
		else
			fprintf(stderr, "   %s %s\n", pOut->sKey, sOutOpts);
	
		
		if(pOut->sSummary != NULL)
			wrapf(stderr, 80, "      ", pOut->sSummary);
		else
			fprintf(stderr, "      (no summary)");
		
		fputs("\n\n", stderr);
		
		pOut++;
	}
	
	
	fputs(
"OPTIONS\n"
"   -h,-?,--help\n"
"      Print this help text and exit returning 0\n"
"\n"
"   -l LOG_LVL,--log=LOG_LVL\n"
"      Set a logging level for the reader, one of [critical, error, warning,\n"
"      info, debug, trace].  The default is info.\n"
"\n", stderr);

	if(bHasInterval)
		fputs(
"   --das2int=OUTPUT\n"
"      Turn on Das 2.1 resolution selection compatibility.  This will cause the\n"
"      first command line argument that does not contain an operator token, and\n"
"      which is not recognized as a special directive, to be treated as the\n"
"      resoluion value for the named OUTPUT.\n"
"\n", stderr);

	fputs(
"   --das2times=SELECTOR\n"
"      Turn on Das 2.1 time range selection compatibility.  This will cause the\n"
"      first two command line arguments that do not contain operater tokens, \n"
"      and which are not recognized as a special directives, to be treated as \n"
"      the '.beg.' and '.end.' values for the named SELECTOR.\n"
"\n", stderr);

	
/*	fputs("Command Line Parameters:\n", stderr); */
	
	const DasSelector* pSel = pSels;
	while(pSel->sKey != NULL){
		
		/* The first line */
		if(pSel->nFlags & ENUM){
			fprintf(stderr, "   %s.eq.STRING", pSel->sKey);
		}
		else{
			/* So we are handling a PARAM, but booleans are different */
			if(pSel->nFmt == bool_t)
				fprintf(stderr, "   %s.eq.BOOL", pSel->sKey);
			else
				fprintf(stderr, "   %s.OP.%s", pSel->sKey, _metaVar(pSel->nFmt));
		}
		
		if(pSel->nFlags & OPTIONAL) fputs(" (optional)\n", stderr);
		else fputs("\n", stderr);
		
		/* The second line */
		if(pSel->nFlags & ENUM){
			fprintf(stderr, "      STRING is one of: %s\n", 
					  _getEnumStr(pSel->psBounds));
		}
		else{
			if(pSel->nFmt == bool_t)
				fprintf(stderr, "      BOOL is one of: true, false\n");
			
			else
				fprintf(stderr, "      Where .OP. is one of: %s\n", 
				  _getSelOpsStr(pSel));
		}
		
		
		/* The option text */
		if(pSel->sSummary != NULL)
			wrapf(stderr, 80, "      ", pSel->sSummary);
		else
			fputs("      (No summary provided)", stderr);
		
		fputs("\n\n", stderr);
		
		pSel++;
	}
	
	/* Now go through the outputs and see if there are any options specific
	   to those items */
	pOut = pOuts;
	while(pOut->sKey != NULL){
		if(pOut->nOpts & OPTIONAL){
			fprintf(stderr, "   %s.out.off (optional)\n", pOut->sKey);
			fprintf(stderr, "      Turn off %s output\n\n", pOut->sKey);
		}
		if(pOut->nOpts & INTERVAL){
			fprintf(stderr, "   %s.int.REAL (optional)\n", pOut->sKey);
			fprintf(stderr, "      Set the output resolution in the %s "
					  "dimension\n\n", pOut->sKey);
		}
		pOut++;
	}
	
	fprintf(stderr, 
"Exit Values:\n"
"  0 - returned to the calling shell if all operations proceeded normally\n"
"    even if there were no data for the given selection parameters.\n"
"\n"
"  %d - returned if there was a problem parsing the command line arguments.\n"
"\n"
"  %d - returned if a library usage error was detected.\n"
"\n", CLI_ERROR, USAGE_ERROR);
	
	
	if(sFooter != NULL)
		wrapf(stderr, 80, NULL, sFooter);
	else
		fprintf(stderr,
"  Other values exit values indicate an unspecified problem.\n\n");
}

/***************************************************************************/
/* Change the command line arguments to match the Das 2.2 spec if needed   */

/* Helper for das21 cl conversion */
void _breakUpLastArg(int* pNumArgs, char*** psArgs){

	int nArgs = *pNumArgs;
	char* sLastArg = (*psArgs)[nArgs - 1];
	int i, j; 
	int nOrigLen = strlen(sLastArg);
	
	/* See much to expand the arg array */
	int nLastArgs = 0;
	bool bLastWasSpace = true;
	for(j = 0; j < nOrigLen; j++){
		if( (!isspace(sLastArg[j]) && bLastWasSpace) ) nLastArgs++;
		bLastWasSpace = isspace(sLastArg[j]);
	}
	
	if(nLastArgs == 1) return;
	
	
	/* Okay, Realloc, null out spaces in the last argument and point the
	   new members of the argument array to the beginning of each non-null
		segment */
	
	int nNewArgs = *pNumArgs - 1 + nLastArgs;
	char** sNewArgs = (char**)calloc(nNewArgs, sizeof(char*));
	
	for(i = 0; i<nArgs - 1; i++) sNewArgs[i] = (*psArgs)[i];
	
	bLastWasSpace = true;
	i = nArgs - 1;
	for(j = 0; j < nOrigLen; j++){
		
		/* If a transition */
		if( (!isspace(sLastArg[j])) && bLastWasSpace ){
			sNewArgs[i] = sLastArg + j;
			i++;
		}
		if(isspace(sLastArg[j])) sLastArg[j] = '\0';
		bLastWasSpace = isspace(sLastArg[j]);
	}
	
	*pNumArgs = nNewArgs;
	*psArgs = sNewArgs;
}

void _setLogLevel(const char* sLevel)
{
	/* Allow Java Logging sounding names as well */
	
	if(strncasecmp(sLevel,"crit",4) == 0){  g_nLogLevel = DAS_LL_CRIT;  return;}
	if(strncasecmp(sLevel,"severe",6) == 0){g_nLogLevel = DAS_LL_CRIT;  return;}
	
	if(strncasecmp(sLevel,"error",5) == 0){ g_nLogLevel = DAS_LL_ERROR; return;}
	
	if(strncasecmp(sLevel,"warn",4) == 0){  g_nLogLevel = DAS_LL_WARN;  return;}
	
	if(strncasecmp(sLevel,"info",4) == 0){  g_nLogLevel = DAS_LL_INFO;  return;}
	if(strncasecmp(sLevel,"notice",6) == 0){g_nLogLevel = DAS_LL_INFO; return; }
	if(strncasecmp(sLevel,"config",6) == 0){g_nLogLevel = DAS_LL_INFO; return; }
	
	if(strncasecmp(sLevel,"debug",5) == 0){ g_nLogLevel = DAS_LL_DEBUG; return;}
	if(strcasecmp(sLevel, "fine") == 0){    g_nLogLevel = DAS_LL_DEBUG; return;}
	
	if(strncasecmp(sLevel,"trace",5) == 0){ g_nLogLevel = DAS_LL_TRACE; return;}
	if(strncasecmp(sLevel,"finer",5) == 0){ g_nLogLevel = DAS_LL_TRACE; return;}
	if(strncasecmp(sLevel,"finest",6) == 0){g_nLogLevel = DAS_LL_TRACE; return;}
	
	fprintf(stderr, "Unknown log level '%s'\n", sLevel);
	exit(CLI_ERROR);
}


void _maybeConvertDas21Cl(const DasSelector* pSels, const DasOutput* pOuts,
		                    int* pNumArgs, char*** psArgs)
{
	int nArgs = *pNumArgs;
	char** sArgs = *psArgs;
	int i = 0, j = 0;
	int iKeyArg = 0;
	int iIntArg = 0;      /* Here Int is short for "Interval" not "integer" */
	const char* sTimeKey = NULL;	
	const char* sIntKey = NULL;
		
	iKeyArg = 0;
	for(i = 1; i< nArgs; i++){
		if(strncmp(sArgs[i], "--das2times", 11) == 0){
			
			iKeyArg = i;
			
			if((sTimeKey = strstr(sArgs[i], "="))==NULL){
				fprintf(stderr, "'=' missing in --das2times argument\n");
				exit(CLI_ERROR);
			}
			
			sTimeKey += 1;
			if((sTimeKey == NULL)||(sTimeKey[0] == '\0')){
				fprintf(stderr, "Key missing in --das2times argument\n");
				exit(CLI_ERROR);
			}
		}					
	}
	
	iIntArg = 0;
	for(i = 1; i< nArgs; i++){
		if(strncmp(sArgs[i], "--das2int", 9) == 0){
			
			iIntArg = i;
			
			if((sIntKey = strstr(sArgs[i], "="))==NULL){
				fprintf(stderr, "'=' missing in --das2int argument\n");
				exit(CLI_ERROR);
			}
			
			sIntKey += 1;
			if((sIntKey == NULL)||(sIntKey[0] == '\0')){
				fprintf(stderr, "Key missing in --das2int argument\n");
				exit(CLI_ERROR);
			}
		}					
	}
	
	/* Is this really unnessesary? --cwp 2016-09-01 */
	if((iIntArg != 0)&&(iKeyArg == 0)){
		fprintf(stderr, "Usage of --das2int requires --das2times\n");
		exit(CLI_ERROR);
	}
	
	if(iKeyArg == 0) return;  /* Das 2.1 compatiblility not requested */
	
	/* Min possible args:  progname --das2time=KEY START END */
	if(nArgs < 4){
		fprintf(stderr, "End time is missing\n");
		exit(CLI_ERROR);
	}
	
	/* Min possible args with intervals: 
	    progname --das2int=OUT --das2time=SEL INTERVAL START END 
	*/
	if(sIntKey != NULL){
		if(nArgs < 6){
			fprintf(stderr, "Interval value missing\n");
			exit(CLI_ERROR);
		}
	}

	/* Check Keys */
	if(_findSelector(pSels, sTimeKey) == NULL){
		fprintf(stderr, "Error in argument %s, selector %s is not defined "
		                "for this reader\n", sArgs[iKeyArg], sTimeKey);
		exit(CLI_ERROR);
	}
	
	
	if(sIntKey != NULL){
		if(_findOutput(pOuts, sIntKey) == NULL){
			fprintf(stderr, "Error in argument %s, output %s is not defined "
					  "for this reader\n", sArgs[iIntArg], sIntKey);
			exit(CLI_ERROR);
		}
	}
	
	/* Remove the --das2times argument (Shift down) */
	for(i = iKeyArg + 1; i < nArgs; i++){
		sArgs[i-1] = sArgs[i];
	}
	nArgs--;
	*pNumArgs = nArgs;
	
	/* Remove the --das2int interval arg (shift down).
	   to be safe find it again instead of calculating it's position */
	if(sIntKey != NULL){
		for(i = 1; i < nArgs; i++){
			if(strncmp(sArgs[i], "--das2int=", 10) == 0){
				iIntArg = i;
				break;
			}
		}
		for(i = iIntArg + 1; i < nArgs; i++){
			sArgs[i-1] = sArgs[i];
		}
		nArgs--;
		*pNumArgs = nArgs;	
	}
	
	
	/* Maybe take the first arg that has no operator and make it the 
	   interval */
	if(sIntKey != NULL){
		for(i = 1; i < nArgs; i++){
			if(_hasOperator(sArgs[i])) continue;
			
			int nLen = strlen(sIntKey) + strlen(DAS_OUT_INTERVAL) + 
					     strlen(sArgs[i]) + 2;
			char* sTmp = (char*)calloc(nLen, sizeof(char));
			sprintf(sTmp, "%s%s%s", sIntKey, DAS_OUT_INTERVAL, sArgs[i]);
			sArgs[i] = sTmp;
			break;
		}	
	}

	
	/* Take the next two arguments without operators and make them the
	   das 2.2 time arguments */
	char* sOp = NULL;
	for(i = 1; i < nArgs; i++){
		if(_hasOperator(sArgs[i])) continue;
		
		if(sOp == NULL)
			sOp = OP_GE;
		else
			sOp = OP_LT;
		
		int nLen = strlen(sArgs[i]) + strlen(sTimeKey) + strlen(sOp) + 2;
		char* sTmp = (char*)calloc(nLen, sizeof(char));
		sprintf(sTmp, "%s%s%s", sTimeKey, sOp, sArgs[i]);
		sArgs[i] = sTmp;
		if( strcmp(sOp, OP_LT) == 0) break;
	}
		
	
	/* In many cases the last argument is just null or whitespace, if that's
	   the case, lop it off and return */
	bool bNonSpace = false;
	for(j = 0; j < strlen(sArgs[nArgs - 1]); j++){
		if(! isspace( sArgs[nArgs - 1][j]) ){
			bNonSpace = true;
			break;
		}
	}
	if(! bNonSpace ){
		sArgs[nArgs - 1] = NULL;
		*pNumArgs = nArgs - 1;
		return;		
	}
		
	/* Reformat the last argument, since we have one */
	_breakUpLastArg(pNumArgs, psArgs);
}

/***************************************************************************/
/* Helper, fill in selector and output information */

DasSelector* _selOrCliError(DasSelector* pSels, const char* sKey, 
		                      const char* sArg)
{
	DasSelector* pSel = pSels;
	
	while(pSel->sKey != NULL){
		if(strcmp(pSel->sKey, sKey) == 0) return pSel;
		pSel++;
	}
	
	fprintf(stderr, "ERROR: In argument '%s', selector '%s' is not defined.\n", 
			  sArg, sKey);
	exit(CLI_ERROR);
	
	return NULL;
}

DasOutput* _outOrCliError(DasOutput* pOuts, const char* sKey,
		                    const char* sArg)
{
	DasOutput* pOut = pOuts;
	
	while(pOut->sKey != NULL){
		if(strcmp(pOut->sKey, sKey) == 0) return pOut;
		pOut++;
	}
	
	fprintf(stderr, "ERROR: In argument '%s', output '%s' is not defined.\n",
			  sArg, sKey);
	exit(CLI_ERROR);
	
	return NULL;
}

int _getOpIdx(DasSelector* pSel, const char* sOp)
{
	if(pSel->nFlags & XLATE_GE_LT){
		if(strcasecmp(sOp, ".beg.") == 0) sOp = OP_GE;
		if(strcasecmp(sOp, ".end.") == 0) sOp = OP_LT;
	}
	
	int i = 0;
	while(pSel->psBounds[i] != NULL){
		if(strcasecmp(sOp, pSel->psBounds[i]) == 0) return i;
		i++;
	}
	return -1;
}

/* Maybe translate outgoing text for .ge. and .lt. selectors */
const char* _xlateBegEnd(bool bTranslate, const char* sOp){
	if(! bTranslate) return sOp;
	
	if(strcasecmp(sOp, OP_GE) == 0) return ".beg.";
	if(strcasecmp(sOp, OP_LT) == 0) return ".end.";
	
	return sOp;
}

/* See if we got all our required parameters set */
void _checkRequired(const DasSelector* pSels)
{
	
	bool bError = false;
	const DasSelector* pCheck = pSels;
	while(pCheck->sKey != NULL){
		
		if(pCheck->nFlags & OPTIONAL){
			pCheck++;
			continue;
		}
		
		if(pCheck->nFlags & ENUM){
			if(pCheck->psValues[0] == NULL){
				fprintf(stderr, "ERROR: Required parameter %s.eq.STRING missing.\n",
						  pCheck->sKey);
				bError = true;
			}
		}
		else{
			
			for(int i = 0; pCheck->psBounds[i] != NULL; i++){
				if(pCheck->psValues[i] == NULL){
					fprintf(stderr, "ERROR: Required parameter %s%s%s missing\n",
							  pCheck->sKey, 
							  _xlateBegEnd(pCheck->nFlags&XLATE_GE_LT, pCheck->psBounds[i]), 
							  _metaVar(pCheck->nFmt));
					bError = true;
				}
			}
		}
		pCheck++;
	}
	
	if(bError){ 
		fprintf(stderr, "ERROR: One or more required command line parameters "
				  "were not specified, use -h for more information.\n");
		exit(USAGE_ERROR);
	}
}

void _parseSelsOuts(int nArgs, char** sArgs, DasSelector* pSels, 
		              DasOutput* pOuts)
{
	const char* pOpBeg = NULL;
	const char* pOpEnd = NULL;
	size_t nTmp = 0;
	
	char sKey[80]      = {'\0'};
	char sOp[32]       = {'\0'};
	char sVal[128]     = {'\0'};
	
	DasSelector* pSel = NULL;
	DasOutput* pOut = NULL;
	
	for(int i = 1; i< nArgs; i++){
		
		memset(sKey, 0, 80);
		memset(sOp, 0, 32);
		memset(sVal, 0, 123);

		pOpBeg = index(sArgs[i], '.');
		pOpEnd = index(pOpBeg + 1, '.');
		
		if((pOpBeg - sArgs[i]) < 79)
			nTmp = pOpBeg - sArgs[i];
		else
			nTmp = 79;

		strncpy(sKey, sArgs[i], nTmp);
		
		if((1 + (pOpEnd - pOpBeg)) < 31)
			nTmp = 1 + (pOpEnd - pOpBeg);
		else
			nTmp = 31;
			
		strncpy(sOp, pOpBeg, nTmp);
		
		if(strlen(pOpEnd + 1) < 127)
			nTmp = strlen(pOpEnd + 1);
		else
			nTmp = 127;
		
		strncpy(sVal, pOpEnd + 1, nTmp);
		
				
		/* This key has to match with something, if it's an output key, try
		   to match it to an output, if its a selector key try to match it to
			a selector */
		if(strcasecmp(sOp, DAS_OUT_INTERVAL) == 0){ 
			pOut = _outOrCliError(pOuts, sKey, sArgs[i]);
			pOut->sInterval = (char*)calloc(strlen(sVal) + 1, sizeof(char));
			strcpy(pOut->sInterval, sVal);
			continue;
		}
		
		if(strcasecmp(sOp, DAS_OUT_SWITCH) == 0){ 
			pOut = _outOrCliError(pOuts, sKey, sArgs[i]);
			
			/* If this output is not disableable, error here */
			if( ! (strcasecmp(sVal, "off") == 0) &&
				 ! (strcasecmp(sVal, "on") == 0) ){
				fprintf(stderr, "ERROR: Unknown value %s in argument %s\n",
						  sVal, sArgs[i]);
				exit(CLI_ERROR);
			}
			
			if( strcasecmp(sVal, "off") == 0){
				if( ! (pOut->nOpts & OPTIONAL)){
					fprintf(stderr, "ERROR: Output '%s' can't be switched off\n", 
							  sKey);
					exit(CLI_ERROR);
				}
				pOut->nOpts = pOut->nOpts & ~DAS_OUT_ENABLE;
			}
			
			if( strcasecmp(sVal, "on") == 0){
				pOut->nOpts = pOut->nOpts | DAS_OUT_ENABLE;
			}
			
			continue;
		}
		
		/* All output operations are handled, now for the boundary operations */
		pSel = _selOrCliError(pSels, sKey, sArgs[i]);
		
		/* Handle enum selectors differently from regular seloctors */
		if(!(pSel->nFlags & ENUM)){
			
			int iOpIdx = _getOpIdx(pSel, sOp);
			
			if(iOpIdx < 0){
				fprintf(stderr, "ERROR: Comparison '%s' isn't allowed for selector"
						  " '%s'.\n", sOp, sKey);
				exit(CLI_ERROR);
			}
			
			pSel->psValues[iOpIdx] = (char*)calloc(strlen(sVal)+1, sizeof(char));
			strcpy(pSel->psValues[iOpIdx], sVal);
		}
		else{
			/* Only operator allowed for ENUMs is .eq. */
			if(strcasecmp(sOp, OP_EQ) != 0){
				fprintf(stderr, "ERROR: Operation '%s' isn't allowed for "
						 "selector '%s'.\n", sOp, sKey);
				exit(CLI_ERROR);
			}
			
			/* make sure the value is in the list of allowed values */
			bool bFound = false;
			for(int j = 0; pSel->psBounds[j] != NULL; j++){
				if(strcmp(sVal, pSel->psBounds[j]) == 0){
					bFound = true;
					break;
				}
			}
			if(!bFound){
				fprintf(stderr, "ERROR: Invalid value '%s' for selector '%s'\n",
						  sVal, sKey);
				exit(CLI_ERROR);
			}
			
			pSel->psValues[0] = (char*)calloc(strlen(sVal) + 1, sizeof(char));
			strcpy(pSel->psValues[0], sVal);
		}
	}
	
	_checkRequired(pSels);
}


/* ************************************************************************* */
/* Parsing Command lines, the main point of the Lib */

void das_parsecmdline(int nArgs, char** sArgs, DasSelector* pSels,
		                DasOutput* pOuts, const char* sDesc, 
		                const char* sRetDesc)
{
	/* The cast, in order of appearence */
	int i = 0;
	int j = 0;
	int jScoot = 0;
		
	/* Should always get at least the program name */
	if((nArgs < 1)||(sArgs == NULL)){
		fprintf(stderr, "USGAGE ERROR in parsecmdline: "
				 "command line has no parameters, %s line %d\n", __FILE__, __LINE__);
		exit(USAGE_ERROR);
	}
		
	if(sArgs == NULL){
		fprintf(stderr, "USAGSE ERROR in parsecmdline: sArgs is null, %s "
		        "line %d", __FILE__, __LINE__);
		exit(USAGE_ERROR);
	}
	
	if(pSels == NULL){
		fprintf(stderr, "USAGSE ERROR in parsecmdline: pSels is null, %s line %d\n",
				  __FILE__, __LINE__);
		exit(USAGE_ERROR);
	}

	if(pOuts == NULL){
		fprintf(stderr, "USAGSE ERROR in parsecmdline: pOuts is null, %s line %d\n",
				  __FILE__, __LINE__);
		exit(USAGE_ERROR);
	}
	
	/* Init value areas for selectors */
	initSelValues(pSels);
	
	/* Init enable setting for outputs */
	initOutMaybeEnable(pOuts);
	
	/* Check selector structures setup */
	checkSelectors(pSels);
	
	/* Check output structures correctness */
	checkOutputs(pOuts);
	
	/* Get the program basename */	
	for(i = strlen(sArgs[0]) - 2; i > 0; i--){
		if(sArgs[0][i] == '/'){
			g_sProgName = sArgs[0] + i + 1;
			break;
		}
	}
	if(g_sProgName == NULL) g_sProgName = sArgs[0];
	
	/* Okay, see if we have any help arguments, putting --help in the 
	   commandline overrides all other actions */
	for(i = 1; i < nArgs; i++){
		if((strcmp(sArgs[i], "-h") == 0)||(strcmp(sArgs[i], "--help") == 0)||
			(strcmp(sArgs[i], "-?") == 0)){
			printHelp(g_sProgName, pSels, pOuts, sDesc, sRetDesc);
			exit(0);
		}
	}
	
	
	/* Check to see if the default log level needs to be adjusted, and 
	   if a log level is set, shift out those parameters */
	int nShift = 0;
	int iKeep = 0;
	for(i = 1; i < nArgs; i++){
		if(strcmp(sArgs[i], "-l") == 0){
			if( i == (nArgs - 1)){
				fprintf(stderr, "Log level missing after -l\n");
				exit(CLI_ERROR);
			}
			_setLogLevel(sArgs[i+1]);
			iKeep = i+2;
			nShift = 2;
			break;
		}
		if(strncmp(sArgs[i], "--log=", 6) == 0){
			_setLogLevel( &(sArgs[i][0]) + 6);
			iKeep = i+1;
			nShift = 1;
			break;
		}
	}
	if(nShift > 0){
		for( ; iKeep < nArgs; iKeep++) sArgs[iKeep - nShift] = sArgs[iKeep];
		nArgs -= nShift;
	}
	
	
	/* See if we've been asked to keepalive */
	for(i = 1; i < nArgs; i++){
		if(strcmp(sArgs[i], "keepalive") == 0){
			fprintf(stderr, "This reader doesn't support keepalive operations\n");
			exit(CLI_ERROR);
		}
	}
	
	/* If you see the --das2int or --das2times compatiblity flags, convert the
	   command line */
	_maybeConvertDas21Cl(pSels, pOuts, &nArgs, &sArgs);
	
	
	/* Find out if any arguments are all whitespace only */
	for(i = 1; i< nArgs; i++){
		bool bNonSpace = false;
		for(j = 0; j < strlen(sArgs[i]); j++){
			if(! isspace( sArgs[i][j]) ){
				bNonSpace = true;
				break;
			}
		}
		if(!bNonSpace){
			fprintf(stderr, "Error, argument number %d only contains whitespace\n", i);
			exit(CLI_ERROR);
		}
	}
	
	/* Scoot non-whitespace characters to the front of the argument */
	for(i = 1; i< nArgs; i++){
		
		jScoot = 0;
		for(j = 0; j < strlen(sArgs[i]); j++){
			if(isspace(sArgs[i][j]))
				jScoot++;
			else
				break;
		}
		if(jScoot != 0){
			int iNull = strlen(sArgs[i]) - jScoot;
			for(j = 0; j < iNull; j++){
				sArgs[i][j] = sArgs[i][j+jScoot];
			}
			sArgs[i][iNull] = '\0';
		}
		
	}
	
	/* Null out trailing non-whitespace characters */
	for(i = 1; i< nArgs; i++){
		for(j = strlen(sArgs[i]) - 1; j > 0; j--){
			if(isspace(sArgs[i][j]))
				sArgs[i][j] = '\0';
			else
				break;
		}
	}
	
	/* Make sure all the remaining arguments are well formed */
	for(i = 1; i< nArgs; i++){
		if( !_hasOperator(sArgs[i]) ){
			fprintf(stderr, "Operator missing in parameter '%s'\n", sArgs[i]);
			exit(CLI_ERROR);
		}
		
		char* pPeriod = index(sArgs[i], '.');
		
		/* make sure there is stuff on both sides */
		if(pPeriod == sArgs[i]){
			fprintf(stderr, "Key missing in parameter '%s'\n", sArgs[i]);
			exit(CLI_ERROR);			
		}
		pPeriod = index(pPeriod + 1, '.');
		if(pPeriod == (sArgs[i] + strlen(sArgs[i])) ){
			fprintf(stderr, "Value missing in parameter '%s'\n", sArgs[i]);
			exit(CLI_ERROR);
		}
	}	
	
	/* Now handle the commandline, it's know to be das 2.2 compatable with
	   no server config or compatibility arguments */
	_parseSelsOuts(nArgs, sArgs, pSels, pOuts);
	
}


/* ************************************************************************* */
/* Get ancillary commandline info */

const char* das_progname(){ return g_sProgName; }

int das_loglevel(){ return g_nLogLevel; }
	
	
/*****************************************************************************/
/* Gathering Values                                                          */
/*****************************************************************************/

const char* das_get_selstr(const DasSelector* pSels, const char* sKey, 
		                 const char* sOp, const char* sDefault)
{
	const DasSelector* pSel;
	pSel = _selOrExit(pSels, sKey);
	
	return das_selstr(pSel, sOp, sDefault);
}

const char* das_selstr(const DasSelector* pSel, const char* sOp, 
                    const char* sDefault)
{
	const char* sOpValue = NULL;
	
	if((strcasecmp(sOp, OP_EQ) != 0) &&
		(strcasecmp(sOp, OP_NE) != 0) &&
		(strcasecmp(sOp, OP_LT) != 0) &&
		(strcasecmp(sOp, OP_GT) != 0) &&
		(strcasecmp(sOp, OP_LE) != 0) &&
		(strcasecmp(sOp, OP_GE) != 0) ){
		
		fprintf(stderr, "USAGSE ERROR: '%s' is not a recognized comparison"
			  " operator , %s line %d\n", sOp, __FILE__, __LINE__);
		exit(USAGE_ERROR);
	}
		
	int i = 0;
	bool bOpAllowed = false;
	while(pSel->psBounds[i] != NULL){
		if(strcasecmp(pSel->psBounds[i], sOp) == 0){
			bOpAllowed = true;
			sOpValue = pSel->psValues[i];
			break;
		}
		i++;
	}
	if(!bOpAllowed){
		fprintf(stderr, "USAGE ERROR: Comparison operator %s isn't allowed for"
				  " selector %s\n", sOp, pSel->sKey);
		exit(USAGE_ERROR);
	}
	
	/* NOTE: The sOpValue may be null, it means no value was stored for
	   this boundary, and that's okay */
	return sOpValue == NULL ? sDefault : sOpValue;	
}

void _wrongFmt(const char* sFunc, const char* sType, const char* sKey){
	fprintf(stderr, "USGAGE ERROR: %s called for non %s Selector '%s'\n",
			  sFunc, sType, sKey);
	exit(USAGE_ERROR);
}

int das_get_selint(const DasSelector* pSels, const char* sKey,
		         const char* sOp, int nDefault)
{
	const DasSelector* pSel = NULL;
	pSel = _selOrExit(pSels, sKey);
	
	return das_selint(pSel, sOp, nDefault);
}

int das_selint(const DasSelector* pSel, const char* sOp, int nDefault)
{
	const char* sValue = NULL;
	int nRet;
		
	if(pSel->nFmt != int_t)
		_wrongFmt("das_get_selint", "int_t", pSel->sKey);
	
	if( (sValue = das_selstr(pSel, sOp, NULL)) == NULL) return nDefault;
	
	if(! _intConv(sValue, &nRet) ){
		fprintf(stderr, "Couldn't convert the value portion of '%s%s%s' to "
				  "an integer\n", pSel->sKey, sOp, sValue);	
		exit(CLI_ERROR);
	}
	return nRet;
}

bool das_get_selbool(const DasSelector* pSels, const char* sKey, bool bDefault)
{
	const DasSelector* pSel = NULL;
	pSel = _selOrExit(pSels, sKey);
	
	return das_selbool(pSel, bDefault);
}

bool das_selbool(const DasSelector* pSel, bool bDefault)
{
	const char* sValue = NULL;
		
	if(pSel->nFmt != bool_t)
		_wrongFmt("das_get_selbool", "bool_t", pSel->sKey);
	
	if( (sValue = das_selstr(pSel, OP_EQ, NULL)) == NULL) return bDefault;
	
	if((strcasecmp(sValue, "true") == 0)||(strcmp(sValue, "1") == 0))
		return true;
	
	if((strcasecmp(sValue, "false") == 0)||(strcmp(sValue, "0") == 0))
		return false;
	
	fprintf(stderr, "Couldn't convert the value portion of '%s.eq.%s' to "
			  "an boolean\n", pSel->sKey, sValue);	
	exit(CLI_ERROR);

	return false; /* Make the compiler happy */
}


double das_get_selreal(const DasSelector* pSels, const char* sKey, 
	                const char* sOp, double rDefault)
{
	const DasSelector* pSel = NULL;
	pSel = _selOrExit(pSels, sKey);
	
	return das_selreal(pSel, sOp, rDefault);
}

double das_selreal(const DasSelector* pSel, const char* sOp, double rDefault)
{
	const char* sValue = NULL;
	double rRet;
	char* endptr = NULL;

		
	if(pSel->nFmt != real_t)
		_wrongFmt("das_get_selreal", "real_t", pSel->sKey);
	
	if( (sValue = das_selstr(pSel, sOp, NULL)) == NULL) return rDefault;
		
	errno = 0;
	
	rRet = strtod(sValue, &endptr); 
	
	if( (errno == ERANGE) || ((errno != 0) && (rRet == 0)) ||
	    (endptr == sValue) ){
		fprintf(stderr, "Couldn't convert value portion of '%s%s%s' to a real\n",
		        pSel->sKey, sOp, sValue);	
		exit(CLI_ERROR);
	}
	return rRet;
}


void das_get_seltime(
	const DasSelector* pSels, const char* sKey, const char* sOp, int* yr, 
	int* mon, int* dom, int* doy, int* hr, int* min, double* sec
){
	const DasSelector* pSel = NULL;
	pSel = _selOrExit(pSels, sKey);
	
	das_seltime(pSel, sOp, yr, mon, dom, doy, hr, min, sec);
}

void das_get_seldastime(
	const DasSelector* pSels, const char* sKey, const char* sOp, das_time* pDt
){
	das_get_seltime(pSels, sKey, sOp, &(pDt->year), &(pDt->month), &(pDt->mday),
	                &(pDt->yday), &(pDt->hour), &(pDt->minute), &(pDt->second));
}


void das_seltime(const DasSelector* pSel, const char* sOp, int* yr, int* mon,
              int* dom, int* doy, int* hr, int* min, double* sec)
{
	const char* sValue = NULL;
	
	if(pSel->nFmt !=timept_t)
		_wrongFmt("das_get_seltime", "timept_t", pSel->sKey);
	
	/* No default is passed in, the pointers are supposed to point to the
	   default values which are changed by this function if the argument was
		supplied */
	
	if( (sValue = das_selstr(pSel, sOp, NULL)) == NULL) return;
	
	int _doy;      /* <-- allows doy pointer in the arg list to be null */
	
	if((yr == NULL)||(mon == NULL)||(dom == NULL)||
		(hr == NULL)||(min == NULL)||(sec == NULL)){
		fprintf(stderr, "USAGE ERROR: Null pointer to das_seltime\n");
		exit(USAGE_ERROR);
	}
	
	if(parsetime(sValue, yr, mon, dom, &_doy, hr, min, sec) != 0){
		fprintf(stderr, "Couldn't parse value portion of '%s%s%s' as a time point.\n",
		        pSel->sKey, sOp, sValue);	
		exit(CLI_ERROR);
	}
	
	if(doy != NULL) *doy = _doy;
}

void das_seldastime(const DasSelector* pSel, const char* sOp, das_time* pDt)
{
	das_seltime(pSel, sOp, &(pDt->year), &(pDt->month), &(pDt->mday),
	            &(pDt->yday), &(pDt->hour), &(pDt->minute), &(pDt->second));
}

const char* das_get_selenum(const DasSelector* pSels, const char* sKey, 
		                      const char* sDefault)
{
	const DasSelector* pSel = NULL;
	pSel = _selOrExit(pSels, sKey);
	
	return das_selenum(pSel, sDefault);
}

const char* das_selenum(const DasSelector* pSel, const char* sDefault)
{
	if(pSel->nFlags & ENUM)
		return pSel->psValues[0] == NULL ?  sDefault : pSel->psValues[0];
	
	fprintf(stderr, "USAGE ERROR: Selector '%s' is not an enumeration\n",
			  pSel->sKey);
	exit(USAGE_ERROR);
}

bool das_get_outenabled(const DasOutput* pOuts, const char* sKey)
{
	const DasOutput* pOut = NULL;
	pOut = _outOrExit(pOuts, sKey);
	
	return das_outenabled(pOut);
}

bool das_outenabled(const DasOutput* pOut)
{
	return pOut->nOpts & DAS_OUT_ENABLE ? true : false;
}

double das_get_outinterval(const DasOutput* pOuts, const char* sKey, 
                          double rDefault)
{
	const DasOutput* pOut = NULL;
	pOut = _outOrExit(pOuts, sKey);
	
	return das_outinterval(pOut, rDefault);
}

double das_outinterval(const DasOutput* pOut, double rDefault)
{
	double rRet;
	char* endptr = NULL;
		
	if(pOut->sInterval == NULL) return rDefault;
	
	errno = 0;
	
	rRet = strtod(pOut->sInterval, &endptr); 
	
	if( (errno == ERANGE) || ((errno != 0) && (rRet == 0)) ||
	    (endptr == pOut->sInterval) ){
		fprintf(stderr, "Couldn't convert value portion of '%s.int.%s' to a real\n",
		        pOut->sKey, pOut->sInterval);	
		exit(CLI_ERROR);
	}
	return rRet;
}
