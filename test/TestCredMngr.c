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
#include <time.h>

#ifndef _WIN32
#include <sys/stat.h>
#define DSEP "/"
#else
#define DSEP "\\"
#endif

#include <das3/core.h>
#include <das3/creds.h>
#include <das3/creds_p.h>   /* tests link the static archive, so they may */

static int g_nFails = 0;

#define FAIL(...) do{ printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); ++g_nFails; }while(0)

static const char* g_sEndPt1 = "https://rogers.place/das/server";
static const char* g_sEndPt2 = "https://rogers.place/das/server/source/trolly/trackcurrent/flex";
static const char* g_sRealm = "Neighborhood of Make-Believe";
static const char* g_sDataset = "Trolly/TrackCurrent";
static const char* g_sHashExpect = "ZHJqZmV2ZXI6cmVhbGx5fjRkaXNjbw==";  /* drjfever:really~4disco */

/* ************************************************************************* */
/* Helpers */

/* Whole small file as a string, "" if it can't be read */
static const char* slurp(const char* sFile)
{
	static char sBuf[16384];
	size_t uRead = 0;
	FILE* pIn = fopen(sFile, "rb");
	if(pIn){ uRead = fread(sBuf, 1, sizeof(sBuf) - 1, pIn); fclose(pIn); }
	sBuf[uRead] = '\0';
	return sBuf;
}

static void writeFile(const char* sFile, const char* sText, int nMode)
{
	FILE* pOut = fopen(sFile, "wb");
	fputs(sText, pOut);
	fclose(pOut);
#ifndef _WIN32
	chmod(sFile, nMode);
#else
	(void)nMode;
#endif
}

static void syncName(const char* sFile, char* sSync, size_t uSync)
{
	snprintf(sSync, uSync, "%s.sync", sFile);
}

/* A key file and the sync file beside it */
static void removeStore(const char* sFile)
{
	char sSync[264];
	syncName(sFile, sSync, sizeof(sSync));
	remove(sFile);
	remove(sSync);
}

static void expectSecret(const DasCredMngr* pMgr, int16_t nCred, const char* sExpect,
	const char* sWhat
){
	char sBuf[DASCRED_SECRET_SZ];
	int nLen = DasCredMngr_getSecret(pMgr, nCred, sBuf, sizeof(sBuf));
	if((nLen != (int)strlen(sExpect)) || (strcmp(sBuf, sExpect) != 0))
		FAIL("%s: secret is '%s' (%d), expected '%s'", sWhat, sBuf, nLen, sExpect);
}

/* The ID of the credential getCred finds, or the negative return */
static int16_t findCred(const DasCredMngr* pMgr, const char* sServer,
	const char* sRealm, const char* sDataset, bool bValid, das_cred* pOut
){
	das_cred cred;
	if(pOut == NULL){
		das_cred_init(&cred, NULL, 0, NULL, 0, NULL, 0);
		pOut = &cred;
	}
	int nRet = DasCredMngr_getCred(pMgr, sServer, sRealm, sDataset, bValid, pOut);
	return (nRet == DAS_OKAY) ? pOut->nCred : (int16_t)nRet;
}

typedef struct prompt_ctx { int nCalls; } prompt_ctx;

static bool testPrompt(
	void* pUser, const char* sServer, const char* sRealm, const char* sDataset,
	const char* sMessage, char* sUser, size_t uUserSz, char* sPass, size_t uPassSz
){
	(void)sServer; (void)sRealm; (void)sDataset; (void)sMessage;
	((prompt_ctx*)pUser)->nCalls += 1;
	snprintf(sUser, uUserSz, "someuser");
	snprintf(sPass, uPassSz, "password1");
	return true;
}

/* ************************************************************************* */
/* Save, load, and the key file's form */

static void test_roundTrip(const char* sDir)
{
	char sFile[256];
	snprintf(sFile, sizeof(sFile), "%s" DSEP "cred_test.txt", sDir);
	removeStore(sFile);

	DasCredMngr* pMgr = new_DasCredMngr();
	if(DasCredMngr_store(pMgr)[0] != '\0') FAIL("A new manager has a store");
	if(DasCredMngr_getPrompt(pMgr, NULL) != NULL) FAIL("A new manager has a prompt");

	DasCredMngr_setFileStore(pMgr, sFile);
	char sDesc[300];
	snprintf(sDesc, sizeof(sDesc), "file:%s", sFile);
	if(strcmp(DasCredMngr_store(pMgr), sDesc) != 0)
		FAIL("Store is '%s', expected '%s'", DasCredMngr_store(pMgr), sDesc);

	if(DasCredMngr_load(pMgr) != 0) FAIL("Loading a missing key file is not 0");

	int16_t n1 = DasCredMngr_addCred(pMgr, g_sEndPt1, g_sRealm, g_sDataset);
	int16_t n2 = DasCredMngr_addCred(pMgr, g_sEndPt2, g_sRealm, NULL);
	if((n1 < 1)||(n2 < 1)||(n1 == n2)) FAIL("Bad IDs %d, %d", n1, n2);
	if(DasCredMngr_addCred(pMgr, g_sEndPt1, g_sRealm, g_sDataset) != n1)
		FAIL("addCred twice made a second record");

	DasCredMngr_setUserPass(pMgr, n1, "drjfever", "really~4disco");
	DasCredMngr_setUserPass(pMgr, n2, "drjfever", "really~4disco");

	if(DasCredMngr_save(pMgr) != 2) FAIL("Save did not write 2 credentials");
	del_DasCredMngr(pMgr);

	char sSync[264];
	syncName(sFile, sSync, sizeof(sSync));
	if(!das_isfile(sSync)) FAIL("No sync file '%s' beside the key file", sSync);

#ifndef _WIN32
	struct stat st;
	if((stat(sFile, &st) != 0)||((st.st_mode & 0777) != 0600))
		FAIL("Key file mode is %o, expected 600", (unsigned)(st.st_mode & 0777));
#endif
	if(strstr(slurp(sFile), "|dataset|Trolly/TrackCurrent|scheme|basic|[000000]"
	          "ZHJqZmV2ZXI6cmVhbGx5fjRkaXNjbw==") == NULL)
		FAIL("Unexpected key file line:\n%s", slurp(sFile));

	pMgr = new_DasCredMngr();
	DasCredMngr_setFileStore(pMgr, sFile);
	if(DasCredMngr_load(pMgr) != 2) FAIL("Load did not find 2 credentials");

	char sServer[DASCRED_SRV_SZ], sRealm[DASCRED_REALM_SZ], sDset[DASCRED_DSET_SZ];
	das_cred cred;
	das_cred_init(&cred, sServer, sizeof(sServer), sRealm, sizeof(sRealm), sDset,
	              sizeof(sDset));

	int16_t nCred = findCred(pMgr, g_sEndPt2, g_sRealm, NULL, true, &cred);
	if(nCred < 1) FAIL("No match for the das3 endpoint");
	else{
		expectSecret(pMgr, nCred, g_sHashExpect, "das3 endpoint");
		if((cred.eScheme != DASAUTH_BASIC)||(cred.uFlags != 0)||
		   !das_datum_isNull(&cred.expires)||(sDset[0] != '\0'))
			FAIL("das3 endpoint record has the wrong scheme, flags or dataset");
	}

	nCred = findCred(pMgr, g_sEndPt1, g_sRealm, g_sDataset, true, &cred);
	if(nCred < 1) FAIL("No match for the das2 dataset");
	else{
		expectSecret(pMgr, nCred, g_sHashExpect, "das2 dataset");
		if(strcmp(sDset, g_sDataset) != 0) FAIL("Dataset copied out as '%s'", sDset);
	}

	/* The secret's length can be asked first */
	if(DasCredMngr_getSecret(pMgr, nCred, NULL, 0) != (int)strlen(g_sHashExpect))
		FAIL("Length query for the secret is wrong");

	/* Short buffers say so */
	char sTiny[8];
	das_cred_init(&cred, sTiny, sizeof(sTiny), NULL, 0, NULL, 0);
	if(DasCredMngr_getCred(pMgr, g_sEndPt1, g_sRealm, g_sDataset, true, &cred)
	   != DASCRED_SHORT)
		FAIL("An 8 byte server buffer is not short");
	if(strcmp(sTiny, "https:/") != 0) FAIL("Short copy is '%s'", sTiny);

	del_DasCredMngr(pMgr);
	removeStore(sFile);
}

static void test_oddLines(const char* sDir)
{
	char sFile[256];
	snprintf(sFile, sizeof(sFile), "%s" DSEP "cred_test2.txt", sDir);

	const char* sText =
		"# Some random text because we think this is a commentable file\n"
		"# Now a totally bogus credential line\n"
		"||||\n"
		"# Something realistic, but wrong\n"
		"\t\tsomeserver\t | some realm | | | | bad hash\n"
		"# Something useful, but with a key this version doesn't know\n"
		"https://a.bad.one | Casey's Place | ID | kitchen | d2Fua2E6d2Fua2E=\n"
		"# Old style, no scheme\n"
		"https://an.old.one | Casey's Place | dataset | kitchen | d2Fua2E6d2Fua2E=\n"
		"# News we can use, the secret pasted in without its flag prefix\n"
		"https://a.good.one:8080/test/server | \tCasey's Place\t | dataset | kitchen | scheme | basic | d2Fua2E6d2Fua2E=\r\n";

	DasCredMngr* pMgr = new_DasCredMngr();
	DasCredMngr_setFileStore(pMgr, sFile);

	/* Quiet the warnings and errors these lines are meant to cause */
	int nOldLvl = daslog_setlevel(DASLOG_NOTHING);

#ifndef _WIN32
	writeFile(sFile, sText, 0644);
	if(DasCredMngr_load(pMgr) != -1) FAIL("A key file others can read was loaded");
#endif
	writeFile(sFile, sText, 0600);
	int nLoaded = DasCredMngr_load(pMgr);

	daslog_setlevel(nOldLvl);

	if(nLoaded != 1) FAIL("Loaded %d credentials, expected 1", nLoaded);

	int16_t nCred = findCred(pMgr, "https://a.good.one:8080/test/server",
	                         "Casey's Place", "kitchen", true, NULL);
	if(nCred < 1) FAIL("No match for the good line");
	else expectSecret(pMgr, nCred, "d2Fua2E6d2Fua2E=", "good line");

	if(findCred(pMgr, "https://a.bad.one", "Casey's Place", NULL, false, NULL) > 0)
		FAIL("A line with an unknown key was used");
	if(findCred(pMgr, "https://an.old.one", "Casey's Place", "kitchen", false, NULL) > 0)
		FAIL("A line with no scheme was used");

	/* Saving keeps lines it didn't use and gives the old one its prefix */
	nOldLvl = daslog_setlevel(DASLOG_NOTHING);
	int nSaved = DasCredMngr_save(pMgr);
	daslog_setlevel(nOldLvl);
	if(nSaved != 1) FAIL("Saved %d credentials, expected 1", nSaved);

	const char* sOut = slurp(sFile);
	if(strstr(sOut, "# Some random text because") == NULL) FAIL("Comment dropped on save");
	if(strstr(sOut, "| ID | kitchen |") == NULL) FAIL("Unknown key line dropped on save");
	if(strstr(sOut, "|dataset|kitchen|scheme|basic|[000000]d2Fua2E6d2Fua2E=") == NULL)
		FAIL("Pasted secret not given its prefix on save:\n%s", sOut);

	del_DasCredMngr(pMgr);
	removeStore(sFile);
}

/* Two managers sharing one key file keep each other's changes */
static void test_merge(const char* sDir)
{
	char sFile[256];
	snprintf(sFile, sizeof(sFile), "%s" DSEP "cred_test3.txt", sDir);
	removeStore(sFile);

	DasCredMngr* pA = new_DasCredMngr();
	DasCredMngr_setFileStore(pA, sFile);
	int16_t nA1 = DasCredMngr_addCred(pA, "https://a.example/one", "R", NULL);
	int16_t nA2 = DasCredMngr_addCred(pA, "https://a.example/two", "R", NULL);
	DasCredMngr_setSecret(pA, nA1, DASAUTH_BEARER, "token.one", NULL);
	DasCredMngr_setSecret(pA, nA2, DASAUTH_BEARER, "token.two", NULL);
	DasCredMngr_save(pA);

	DasCredMngr* pB = new_DasCredMngr();
	DasCredMngr_setFileStore(pB, sFile);
	DasCredMngr_load(pB);
	int16_t nB3 = DasCredMngr_addCred(pB, "https://b.example/three", "R", NULL);
	DasCredMngr_setSecret(pB, nB3, DASAUTH_BEARER, "token.three", NULL);
	DasCredMngr_save(pB);

	/* A removes one and saves; B's addition must survive */
	if(DasCredMngr_removeCred(pA, nA2) != DAS_OKAY) FAIL("removeCred failed");
	if(DasCredMngr_getSecret(pA, nA2, NULL, 0) != DASCRED_NONE)
		FAIL("A removed credential still has a secret");
	if(DasCredMngr_numCreds(pA) != 1) FAIL("numCreds after a removal is not 1");
	if(DasCredMngr_save(pA) != 2) FAIL("Merged save did not write 2 credentials");

	const char* sOut = slurp(sFile);
	if(strstr(sOut, "token.three") == NULL) FAIL("Another process's credential was lost");
	if(strstr(sOut, "token.two") != NULL) FAIL("A removed credential was saved");
	if(findCred(pA, "https://b.example/three", "R", NULL, true, NULL) < 1)
		FAIL("Save did not take in the other process's credential");

	/* Load in B does not bring back what A removed, and IDs aren't reused */
	DasCredMngr_load(pB);
	if(findCred(pB, "https://a.example/two", "R", NULL, false, NULL) > 0)
		FAIL("B still has the credential A removed and saved");
	int16_t nNew = DasCredMngr_addCred(pA, "https://a.example/two", "R", NULL);
	if((nNew < 1)||(nNew == nA2)) FAIL("Re-adding reused ID %d", nNew);

	/* Listing walks every live credential */
	das_cred cred;
	das_cred_init(&cred, NULL, 0, NULL, 0, NULL, 0);
	int nListed = 0;
	for(int i = 0; i < DasCredMngr_numCreds(pA); ++i)
		if(DasCredMngr_getCredByIdx(pA, i, &cred) == DAS_OKAY) ++nListed;
	if(nListed != 3) FAIL("Listed %d credentials, expected 3", nListed);
	if(DasCredMngr_getCredByIdx(pA, nListed, &cred) != DASCRED_NONE)
		FAIL("Listing past the end is not DASCRED_NONE");

	del_DasCredMngr(pA);
	del_DasCredMngr(pB);
	removeStore(sFile);
}

/* ************************************************************************* */
/* Matching, rejection, expiration, prompting */

static void test_matching(void)
{
	DasCredMngr* pMgr = new_DasCredMngr();
	const char* sSrv = "https://x.example/das/server";

	int16_t nWide = DasCredMngr_addCred(pMgr, sSrv, "Realm", NULL);
	int16_t nDset = DasCredMngr_addCred(pMgr, sSrv, "Realm", "Juno/WAV/Survey");
	int16_t nAny  = DasCredMngr_addCred(pMgr, "https://y.example/flex", NULL, NULL);
	DasCredMngr_setSecret(pMgr, nWide, DASAUTH_BASIC, "d2lkZTp3aWRl", NULL);
	DasCredMngr_setSecret(pMgr, nDset, DASAUTH_BASIC, "ZHNldDpkc2V0", NULL);
	DasCredMngr_setSecret(pMgr, nAny, DASAUTH_BEARER, "abc.def-ghi_jkl~", NULL);

	if(findCred(pMgr, sSrv, "Realm", "Juno/WAV/Survey", true, NULL) != nDset)
		FAIL("A dataset specific credential did not win");
	if(findCred(pMgr, sSrv, "Realm", "Cassini/RPWS/Survey", true, NULL) != nWide)
		FAIL("Another dataset did not get the server wide credential");
	if(findCred(pMgr, sSrv, "Realm", NULL, true, NULL) != nWide)
		FAIL("No dataset matched a dataset specific credential");
	if(findCred(pMgr, sSrv, "Other Realm", NULL, true, NULL) != DASCRED_NONE)
		FAIL("A different realm matched");
	if(findCred(pMgr, "https://y.example/flex", "Whatever", NULL, true, NULL) != nAny)
		FAIL("A record with no realm did not match a request with one");

	char sAuth[DASCRED_AUTH_SZ];
	if((_DasCredMngr_getHttpAuth(pMgr, "https://y.example/flex", NULL, NULL, sAuth,
	    sizeof(sAuth)) != nAny) || (strcmp(sAuth, "Bearer abc.def-ghi_jkl~") != 0))
		FAIL("Bearer header value is '%s'", sAuth);

	/* Characters that could split a header or the key file are refused */
	if(DasCredMngr_setSecret(pMgr, nAny, DASAUTH_BEARER, "abc\r\nX-Evil: 1", NULL) <= 0)
		FAIL("A secret with a line break was taken");
	if(DasCredMngr_addCred(pMgr, "https://z.example", "a|b", NULL) != -1)
		FAIL("A realm with '|' was taken");
	expectSecret(pMgr, nAny, "abc.def-ghi_jkl~", "after a refused secret");

	del_DasCredMngr(pMgr);
}

static void test_states(void)
{
	DasCredMngr* pMgr = new_DasCredMngr();
	const char* sSrv = "https://x.example/das/server";
	prompt_ctx ctx = {0};
	char sAuth[DASCRED_AUTH_SZ];

	/* Nothing stored and no prompt: nothing to send */
	if(_DasCredMngr_getHttpAuth(pMgr, sSrv, "R", NULL, sAuth, sizeof(sAuth)) != DASCRED_NONE)
		FAIL("getHttpAuth with no credential and no prompt found something");

	DasCredMngr_setPrompt(pMgr, testPrompt, &ctx);
	void* pUser = NULL;
	if((DasCredMngr_getPrompt(pMgr, &pUser) != testPrompt)||(pUser != &ctx))
		FAIL("getPrompt does not return what was set");

	int16_t nCred = _DasCredMngr_getHttpAuth(pMgr, sSrv, "R", NULL, sAuth, sizeof(sAuth));
	if((nCred < 1)||(ctx.nCalls != 1)||(strcmp(sAuth, "Basic c29tZXVzZXI6cGFzc3dvcmQx") != 0))
		FAIL("Prompted login gave '%s' after %d prompts", sAuth, ctx.nCalls);

	_DasCredMngr_getHttpAuth(pMgr, sSrv, "R", NULL, sAuth, sizeof(sAuth));
	if(ctx.nCalls != 1) FAIL("A stored login prompted again");

	/* A refused secret is not sent again on its own */
	_DasCredMngr_authFailed(pMgr, nCred, "Credentials not accepted by remote server");
	das_cred cred;
	das_cred_init(&cred, NULL, 0, NULL, 0, NULL, 0);
	if((findCred(pMgr, sSrv, "R", NULL, false, &cred) != nCred)||
	   !(cred.uFlags & DASCRED_REJECTED))
		FAIL("authFailed did not flag the credential");
	if(findCred(pMgr, sSrv, "R", NULL, true, NULL) != DASCRED_NONE)
		FAIL("A rejected credential still counts as valid");
	_DasCredMngr_getHttpAuth(pMgr, sSrv, "R", NULL, sAuth, sizeof(sAuth));
	if(ctx.nCalls != 2) FAIL("A rejected login did not lead to a prompt");
	if(findCred(pMgr, sSrv, "R", NULL, true, NULL) != nCred)
		FAIL("The new login did not clear the rejection");

	/* An expired one is the same */
	int64_t nPast = (int64_t)time(NULL) - 3600;
	das_datum expires;
	das_datum_init(&expires, (const ubyte*)&nPast, vtLong, 0, UNIT_T1970);
	DasCredMngr_setSecret(pMgr, nCred, DASAUTH_BEARER, "old.token", &expires);
	if((findCred(pMgr, sSrv, "R", NULL, false, &cred) != nCred)||
	   !(cred.uFlags & DASCRED_EXPIRED)||das_datum_isNull(&cred.expires))
		FAIL("Expired token not flagged");
	DasCredMngr_setPrompt(pMgr, NULL, NULL);
	if(_DasCredMngr_getHttpAuth(pMgr, sSrv, "R", NULL, sAuth, sizeof(sAuth)) != DASCRED_NONE)
		FAIL("An expired token was sent");

	/* DASAUTH_NONE clears the secret but keeps the record */
	DasCredMngr_setSecret(pMgr, nCred, DASAUTH_NONE, NULL, NULL);
	if((findCred(pMgr, sSrv, "R", NULL, false, &cred) != nCred)||
	   (cred.eScheme != DASAUTH_NONE)||(DasCredMngr_getSecret(pMgr, nCred, NULL, 0) != 0))
		FAIL("Clearing a secret did not leave an empty record");

	if(DasCredMngr_getSecret(pMgr, 9999, NULL, 0) != DASCRED_NONE)
		FAIL("An unknown ID found a secret");

	del_DasCredMngr(pMgr);
}

/* Replaced secrets are tombstoned and the array rebuilt once dead rows
   outweigh live ones; every record must still find its own secret after */
static void test_compact(void)
{
	DasCredMngr* pMgr = new_DasCredMngr();
	int16_t nKeep = DasCredMngr_addCred(pMgr, "https://keep.example", NULL, NULL);
	int16_t nTurn = DasCredMngr_addCred(pMgr, "https://turn.example", NULL, NULL);
	DasCredMngr_setSecret(pMgr, nKeep, DASAUTH_BEARER, "keep.me", NULL);

	static char sTok[3001];
	for(int i = 0; i < 6; ++i){
		memset(sTok, 'a' + i, 3000);
		sTok[3000] = '\0';
		DasCredMngr_setSecret(pMgr, nTurn, DASAUTH_BEARER, sTok, NULL);
	}
	expectSecret(pMgr, nKeep, "keep.me", "after compaction");
	expectSecret(pMgr, nTurn, sTok, "turned over secret");
	del_DasCredMngr(pMgr);
}

/* A hash made from a user name and password is a C string.  The encoder's
   output lengths here (24, 8 and 36 bytes) include ones with no slack in a
   typical heap block, so a missing terminator shows under valgrind. */
static void test_base64(void)
{
	const char* aPair[][3] = {
		{"someuser",    "password1",       "c29tZXVzZXI6cGFzc3dvcmQx"},
		{"al",          "pw",              "YWw6cHc="},
		{"a_user_name", "a_long_password", "YV91c2VyX25hbWU6YV9sb25nX3Bhc3N3b3Jk"},
	};
	DasCredMngr* pMgr = new_DasCredMngr();
	int16_t nCred = DasCredMngr_addCred(pMgr, "https://a.new.one/server", "The Realm", NULL);

	for(int i = 0; i < 3; ++i){
		char sPair[64];
		snprintf(sPair, sizeof(sPair), "%s:%s", aPair[i][0], aPair[i][1]);
		size_t uLen = 0;
		char* sEnc = das_b64_encode((const unsigned char*)sPair, strlen(sPair), &uLen);
		if((sEnc == NULL) || (uLen != strlen(aPair[i][2])) || (strcmp(sEnc, aPair[i][2]) != 0))
			FAIL("base64 of '%s' is not the string '%s'", sPair, aPair[i][2]);
		free(sEnc);

		if(DasCredMngr_setUserPass(pMgr, nCred, aPair[i][0], aPair[i][1]) != DAS_OKAY)
			FAIL("Could not set user %s", aPair[i][0]);
		expectSecret(pMgr, nCred, aPair[i][2], aPair[i][0]);
	}

	if(DasCredMngr_setUserPass(pMgr, nCred, "bad:name", "pw") <= 0)
		FAIL("A user name with a colon was taken");

	del_DasCredMngr(pMgr);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	/* Errors return and are saved, several tests cause them on purpose */
	das_init(argv[0], DASERR_DIS_RET, 1024, DASLOG_INFO, NULL);

	if(argc < 2){
		printf("ERROR: Working directory not provided on the command line\n");
		return 64;
	}

	test_roundTrip(argv[1]);
	test_oddLines(argv[1]);
	test_merge(argv[1]);
	test_matching();
	test_states();
	test_compact();
	test_base64();

	if(g_nFails > 0){
		printf("ERROR: TestCredMngr had %d failure(s)\n", g_nFails);
		return 64;
	}
	daslog_info("All credentials handling tests passed.");
	return 0;
}
