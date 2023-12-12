/* Test saving and reading authentication keys */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code and is meant to explain an
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
#include <string.h>

#include <das2/core.h>

#define PROG_ERR 64

/* ************************************************************************* */

int main(int argc, char** argv){

	/* Exit on errors, log info messages and up, don't install a log handler */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	if(argc < 2){
		return das_error(PROG_ERR, "Working directory not provided on the command line");
	}

	char sFile[128] = {'\0'};
	snprintf(sFile, 127, 
#ifndef _WIN32
		"%s/cred_test.txt",
#else
		"%s\\cred_test.txt",
#endif
		argv[1]
	);

	DasCredMngr* pMngr = new_CredMngr(sFile);

	const char* sEndPt1 = "https://rogers.place/das/server";
	const char* sRealm = "Neighborhood of Make-Believe";
	const char* sDataset = "Trolly/TrackCurrent";
	const char* sUser  = "drjfever";
	const char* sPass  = "really~4disco";

	const char* sHashExpect = "ZHJqZmV2ZXI6cmVhbGx5fjRkaXNjbw==";
	CredMngr_addUserPass(pMngr, sEndPt1, sRealm, sDataset, sUser, sPass);


	const char* sEndPt2 = "https://rogers.place/das/server/source/trolly/trackcurrent/flex";
	CredMngr_addUserPass(pMngr, sEndPt2, sRealm, NULL, sUser, sPass);

	CredMngr_save(pMngr, NULL, NULL);
	del_CredMngr(pMngr); pMngr = NULL;

	pMngr = new_CredMngr(sFile);

	CredMngr_load(pMngr, NULL, NULL);

	/* Expect a vaild credential */
	const das_credential* pCred;

	if((pCred = CredMngr_getCred(pMngr,sEndPt2, sRealm, NULL, true))== NULL)
		return das_error(PROG_ERR, "No matching credential found");
	
	if(strcmp(pCred->sHash, sHashExpect) != 0){
		return das_error(PROG_ERR, "Credential hash mis-match");
	}
	
	if((pCred = CredMngr_getCred(pMngr, sEndPt1, sRealm, sDataset, true))== NULL)
		return das_error(PROG_ERR, "No matching credential found");

	if(strcmp(pCred->sHash, sHashExpect) != 0)
		return das_error(PROG_ERR, "Credential hash mis-match");
	

	del_CredMngr(pMngr); pMngr = NULL;

	/* Try again with various odd credentials lines */
		snprintf(sFile, 127, 
#ifndef _WIN32
		"%s/cred_test2.txt",
#else
		"%s\\cred_test2.txt",
#endif
		argv[1]
	);

	FILE* pOut = fopen(sFile, "wb");

	fprintf(pOut, "# Some random text becasue we think this is a commentable file\n");
	fprintf(pOut, "# Now a totally bogus credential line\n");
	fprintf(pOut, "||||\n");
	fprintf(pOut, "# Something realistic, but wrong\n");
	fprintf(pOut, "\t\tsomeserver\t | some realm | | | | bad hash\n");
	fprintf(pOut, "# Something useful, but also wrong\n");
	fprintf(pOut, "https://a.bad.one | Casey's Place | ID | kitchen | d2Fua2E6d2Fua2E=\n");
	fprintf(pOut, "# News we can use\n");
	fprintf(pOut, "https://a.good.one:8080/test/server | \tCasey's Place\t | dataset | kitchen | d2Fua2E6d2Fua2E=\r\n");

	fclose(pOut);

	pMngr = new_CredMngr(NULL);  // Starts off with $HOME/.das2_auth

	// Switch the log level so that intentional errors don't show
	int nOldLvl = daslog_setlevel(daslog_strlevel("error"));

	CredMngr_load(pMngr, NULL, sFile); // Switches to new location

	// Now switch it back
	daslog_setlevel(nOldLvl);	

	if(strcmp(pMngr->sKeyFile, sFile) != 0)
		return das_error(PROG_ERR, "Failed to switch to new credentials location");

	
	if((pCred = CredMngr_getCred(pMngr,
		"https://a.good.one:8080/test/server", "Casey's Place", "kitchen", true
	))== NULL)
		return das_error(PROG_ERR, "No matching credential found");

	if(strcmp(pCred->sHash, "d2Fua2E6d2Fua2E=") != 0)
		return das_error(PROG_ERR, "Credential hash mis-match");

	del_CredMngr(pMngr);

	daslog_info("All credentials handling tests passed.");

	return 0;
}