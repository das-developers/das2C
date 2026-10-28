/** @file TestArrays.c Unit tests for federated catalog handling */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this file, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this file dedicate any and all copyright interest in this file to 
 * the public domain. We make this dedication for the benefit of the
 * public at large and to the detriment of our heirs and successors. We
 * intend this dedication to be an overt act of relinquishment in
 * perpetuity of all present and future rights to this file under
 * copyright law.
 *
 * THIS FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/json.h>
#include <das3/node.h>
#include <das3/catalog.h>

/*  An example fully qualified das federated catalog note fragment:

   "tag:das2.org,2012:site:/uiowa/cassini/ephemeris/dione#SOURCE/ACCESS/0/BASE_URL" 

   Kind of cool eh?
*/

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

	/* Children are loaded by a session, which holds the agent string and
	   the node cache; the nodes themselves no longer carry either */
	DasCat* pCat = new_DasCat(NULL, sAgent);
	
	/* Simple catalog loads, direct URLs */
	sUrl = "https://das2.org/catalog/das/site/uiowa/cassini/ephemeris/saturn.json";
	
	/* In the global catalog this node is named:
	 *
	 *   tag:das2.org,2012:site:/uiowa/ephemeris/saturn
	 *
	 * But we are loading it by location, as an isolated item, so it is a
	 * root of its own with no URI.  The session owns
	 * every node it loads; del_DasCat() at the end frees them all.
	 */
	DasNode* pCasEphem = DasCat_resolve(pCat, sUrl);
	nTest += info_or_exit(sUrl, pCasEphem, "Cassini Saturn Ephemeris", nTest);
	
	sUrl = "https://das2.org/catalog/das/site/uiowa/juno/wav.json";
	DasNode* pWavCat = DasCat_resolve(pCat, sUrl);
	nTest += info_or_exit(sUrl, pWavCat, "Juno Waves Catalog", nTest);
	
	const DasNode* pSurvey = DasCat_subNode(pCat, pWavCat, "survey");
	nTest += info_or_exit(sUrl, pSurvey, "Juno Waves Survey Data Source", nTest);
	
	printf("TEST %d: (Not) Deleting a non-root ", nTest);
	del_DasNode((DasNode*)pSurvey);  /* Erasing const for testing purposes */
	                                  /* Shouldn't be done in app code      */
	if(DasNode_srcUrl(pSurvey)[0] != '\0') printf(" ...looks like it's still here [OKAY]\n");
	else printf("... [FAILED]");
	nTest += 1;
	
	/* The compiled in seed, the root of the das catalog */
	DasNode* pRoot = DasCat_seed(pCat);
	/* Can't call info or exit because the root has no URI */
	if(pRoot == NULL){
		printf("TEST %d: Find and load root node   [FAILED]\n", nTest);
		return nTest;
	}
	else{
		printf("TEST %d: Root node loaded from %s [OKAY]\n", nTest, DasNode_srcUrl(pRoot));
	}
	nTest += 1;
	
	/* Extra cassini check */
	sUrl = "tag:das2.org,2012:site:/uiowa/cassini/rpws/survey";
	const DasNode* pCasSur = DasCat_subNode(pCat, pRoot, sUrl);
	nTest += info_or_exit(sUri, pCasSur, "Cassini/survey", nTest);

		
	/*das root */
	sUri = "tag:das2.org,2012:";
	const DasNode* pDas2 = DasCat_subNode(pCat, pRoot, sUri);
	nTest += info_or_exit(sUri, pDas2, "Root", nTest);
	
	/* spase root */
	sUri = "tag:spase-group.org,2018:spase://";
	const DasNode* pSpase = DasCat_subNode(pCat, pRoot, sUri);
	nTest += info_or_exit(sUri, pSpase, "Root", nTest);
	
	/* cdaweb root */
	sUri = "tag:cdaweb.sci.gsfc.nasa.gov,2018:";
	const DasNode* pCdaWeb = DasCat_subNode(pCat, pRoot, sUri);
	nTest += info_or_exit(sUri, pCdaWeb, "Root", nTest);
	
	/* Deep dig from existing root */
	sUri = "tag:das2.org,2012:site:/uiowa/juno/wav/uncalibrated/hrs";
	const DasNode* pWavUcalHrs = DasCat_subNode(pCat, pRoot, sUri);
	nTest += info_or_exit(sUri, pWavUcalHrs, "Juno Waves Uncalibrated HFWBR", nTest);
	
	/* A deep path through the session: the levels walked above stay loaded */
	sUri = "tag:das2.org,2012:site:/uiowa/juno/wav/survey/das2";
	DasNode* pSurvey2 = DasCat_resolve(pCat, sUri);
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
	
	/* TODO: Add more JSON catalog tests */
	del_DasCat(pCat);
	return 0;
}

