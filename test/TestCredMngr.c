/* Test saving and reading authentication keys */

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
#include <string.h>

#include <das3/core.h>

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

	/* Expect a valid credential */
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

	/* A hash made from a user name and password is a C string.  The encoder's
	   output lengths here (24, 8 and 36 bytes) include ones with no slack in a
	   typical heap block, so a missing terminator shows under valgrind. */
	const char* aPair[][3] = {
		{"someuser",    "password1",       "c29tZXVzZXI6cGFzc3dvcmQx"},
		{"al",          "pw",              "YWw6cHc="},
		{"a_user_name", "a_long_password", "YV91c2VyX25hbWU6YV9sb25nX3Bhc3N3b3Jk"},
	};
	for(int i = 0; i < 3; ++i){
		char sPair[64];
		snprintf(sPair, sizeof(sPair), "%s:%s", aPair[i][0], aPair[i][1]);
		size_t uLen = 0;
		char* sEnc = das_b64_encode((const unsigned char*)sPair, strlen(sPair), &uLen);
		if((sEnc == NULL) || (uLen != strlen(aPair[i][2])) || (strcmp(sEnc, aPair[i][2]) != 0))
			return das_error(PROG_ERR, "base64 of '%s' is not the string '%s'",
				sPair, aPair[i][2]);
		free(sEnc);

		if(CredMngr_addUserPass(pMngr, "https://a.new.one/server", "The Realm",
			NULL, aPair[i][0], aPair[i][1]) < 1)
			return das_error(PROG_ERR, "Could not add user %s", aPair[i][0]);

		pCred = CredMngr_getCred(pMngr, "https://a.new.one/server", "The Realm", NULL, false);
		if((pCred == NULL) || (strcmp(pCred->sHash, aPair[i][2]) != 0))
			return das_error(PROG_ERR, "Stored hash for user %s is not '%s'",
				aPair[i][0], aPair[i][2]);
	}

	del_CredMngr(pMngr);

	daslog_info("All credentials handling tests passed.");

	return 0;
}