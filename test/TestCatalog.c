/** @file TestArrays.c Unit tests for federated catalog handling */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code that intends to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestBuilder.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <das2/core.h>

/*  "tag:das2.org,2012:site:/uiowa/cassini/ephemeris/dione#SOURCE/ACCESS/0/BASE_URL" */
  
  

int info_or_exit(
	const char* sSrc, const DasNode* pNode, const char* sWhich, int nTest
){
	
	printf("TEST %d: ", nTest);
	const char* sName = pNode ? DasNode_name(pNode) : NULL ;
	const char* sUri  = pNode ? DasNode_pathUri(pNode) : NULL;
	const char* sUrl  = pNode ? DasNode_srcUrl(pNode) : NULL;
	
	if(pNode == NULL || sName == NULL || sUri == NULL || sUrl == NULL){
		printf("Load %s from %s    [FAILED]\n", sSrc, sWhich);
		exit(nTest + 1);
	}
	
	printf("Loaded Node %s (%s) from %s   [OKAY]\n", sName, sUri, sUrl);
	return 1;
}


int main(int argc, char** argv) {

	int nTest = 1;
	const char* sUri = NULL;
	const char* sUrl = NULL;
	const char* sAgent = "libdas2/2.3 Unit_Test";
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	/* Simple catalog loads, direct URLs */
	sUrl = "http://das2.org/catalog/das/site/uiowa/cassini/ephemeris/saturn.json";
	
	/* In the global catalog this node is name:
	 *
	 *   tag:das2.org,2012:site:/uiowa/ephemeris/saturn
	 * 
	 * But were are loading it is if it was an isolated item so we can name it 
	 * whatever we want, hence doggy
	 */
	DasNode* pCasEphem = new_RootNode_url(sUrl, "doggy", NULL, sAgent);
	nTest += info_or_exit(sUrl, pCasEphem, "Cassini Saturn Ephemeris", nTest);
	
	printf("TEST %d: Deleting a single node", nTest);
	del_RootNode(pCasEphem); pCasEphem = NULL;
	printf("    [OKAY]\n");
	nTest += 1;
	
	sUrl = "http://das2.org/catalog/das/site/uiowa/juno/wav.json";
	DasNode* pWavCat = new_RootNode_url(sUrl, "whattie", NULL, sAgent);
	nTest += info_or_exit(sUrl, pWavCat, "Juno Waves Catalog", nTest);
	
	const DasNode* pSurvey = DasNode_subNode(pWavCat, "survey", NULL, sAgent);
	nTest += info_or_exit(sUrl, pSurvey, "Juno Waves Survey Data Source", nTest);
	
	printf("TEST %d: (Not) Deleting a non-root ", nTest);
	del_RootNode((DasNode*)pSurvey);  /* Erasing const for testing purposes */
	                                  /* Shouldn't be done in app code      */
	if(pSurvey->sURL[0] != '\0') printf(" ...looks like it's still here [OKAY]\n");
	else printf("... [FAILED]");
	nTest += 1;
	
	printf("TEST %d: Deleting a catalog node", nTest);
	del_RootNode(pWavCat); pWavCat = NULL; pSurvey = NULL;
	printf("    [OKAY]\n");
	nTest += 1;
	
	
	/* Central root catalog*/
	DasNode* pRoot = new_RootNode(sUri, NULL, sAgent);
	/* Can't call info or exit because root URI is null */
	if(pRoot == NULL){
		printf("TEST %d: Find and load root node   [FAILED]\n", nTest);
		return nTest;
	}
	else{
		printf("TEST %d: Root node loaded from %s [OKAY]\n", nTest, pRoot->sURL);
	}
	nTest += 1;
	
	/* Extra cassini check */
	sUrl = "tag:das2.org,2012:site:/uiowa/cassini/rpws/survey";
	const DasNode* pCasSur = DasNode_subNode(pRoot, sUrl, NULL, sAgent);
	nTest += info_or_exit(sUri, pCasSur, "Cassini/survey", nTest);

		
	/*das2 root */
	sUri = "tag:das2.org,2012:";
	const DasNode* pDas2 = DasNode_subNode(pRoot, sUri, NULL, sAgent);
	nTest += info_or_exit(sUri, pDas2, "Root", nTest);
	
	/* spase root */
	sUri = "tag:spase-group.org,2018:spase://";
	const DasNode* pSpase = DasNode_subNode(pRoot, sUri, NULL, sAgent);
	nTest += info_or_exit(sUri, pSpase, "Root", nTest);
	
	/* cdaweb root */
	sUri = "tag:cdaweb.sci.gsfc.nasa.gov,2018:";
	const DasNode* pCdaWeb = DasNode_subNode(pRoot, sUri, NULL, sAgent);
	nTest += info_or_exit(sUri, pCdaWeb, "Root", nTest);
	
	/* Deep dig from existing root */
	sUri = "tag:das2.org,2012:site:/uiowa/juno/wav/uncalibrated/hrs";
	const DasNode* pWavUcalHrs = DasNode_subNode(pRoot, sUri, NULL, sAgent);
	nTest += info_or_exit(sUri, pWavUcalHrs, "Juno Waves Uncalibrated HFWBR", nTest);
	
	/* deleting top root */
	printf("TEST %d: Deleting URI based top root ", nTest);
	del_RootNode(pRoot); 
	pRoot = NULL; pDas2 = pSpase = pCdaWeb = pWavUcalHrs = NULL;
	printf("    [OKAY]\n");
	nTest += 1;
	
	
	/* direct deep dig to ferret out memory leaks */
	sUri = "tag:das2.org,2012:site:/uiowa/juno/wav/survey/das2";
	DasNode* pSurvey2 = new_RootNode(sUri, NULL, sAgent);
	nTest += info_or_exit(sUri, pSurvey2, "Waves Survey Deep Read", nTest);
	
	/* Get a Query definition */
	printf("TEST %d: Getting partial query interface ", nTest);
	const DasJdo* pQuery = DasNode_getJdo(pSurvey2, "protocol/http_params/start_time");
	if(pQuery == NULL){ 
		printf("   [FAILED]\n");
		return nTest;
	}
	else{ 
		printf("   [OKAY]\n");
	}
	nTest += 1;
	
	/* And remove it */
	printf("TEST %d: Deleting deep URI direct lookup", nTest);
	del_RootNode(pSurvey2);
	printf("   [OKAY]\n");
	
	/* TODO: Add more JSON catalog tests */
	return 0;
}

