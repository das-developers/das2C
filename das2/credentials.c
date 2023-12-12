/* Copyright (C) 2017-2023 Chris Piker <chris-piker@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"
#include "log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "credentials.h"

/* More stack overflow copy-pasta.  This is getting embarrassing but gets the
 * job done. 
 * 
 * Source page: https://stackoverflow.com/questions/1413445/reading-a-password-from-stdcin
 * User Credit: Vargas
 */

void SetStdinEcho(bool enable)
{
#ifdef WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE); 
    DWORD mode;
    GetConsoleMode(hStdin, &mode);

    if( !enable )
        mode &= ~ENABLE_ECHO_INPUT;
    else
        mode |= ENABLE_ECHO_INPUT;

    SetConsoleMode(hStdin, mode );

#else
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if( !enable )
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;

    (void) tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}

bool das_term_prompt(
	const char* sServer, const char* sRealm, const char* sDataset, 
	const char* sMessage, char* sUser, char* sPassword
){
	fprintf(stderr, "Authentication Required\n");
	if(sMessage)
		fprintf(stderr, "NOTE:    %s\n", sMessage);
	if(sServer != NULL)  fprintf(stderr, "   Server:  %s\n", sServer);
	if(sRealm != NULL)   fprintf(stderr, "   Realm:   %s\n", sRealm);
	if(sDataset != NULL) fprintf(stderr, "   Dataset: %s\n", sDataset);
	
	fprintf(stderr, "Login Name > ");
	scanf("%127s", sUser);
	SetStdinEcho(false);
	fprintf(stderr, "Password > ");
	scanf("%127s", sPassword);
	fprintf(stderr, "\n");
	SetStdinEcho(true);
			  
	return true;
}


/* Simple base 64 encoding (maybe too simple, probably dosen't handle 
 * utf-8 correctly)
 * Gangted from stackoverflow at url:
 * 
 * https://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
 * 
 * Posted by user: ryyst
 * 
 */
static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

char* das_b64_encode(
	const unsigned char *data, size_t input_length, size_t *output_length
) {

    *output_length = 4 * ((input_length + 2) / 3);

    char *encoded_data = calloc(*output_length, sizeof(char));
    if (encoded_data == NULL) return NULL;

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    return encoded_data;
}


bool das_cred_init(
	das_credential* pCred, const char* sServer, const char* sRealm, 
	const char* sDataset, const char* sHash
){
	
	// Check all the sizes
	size_t uLen;
	const char* sWhich = NULL;
	
	if(sServer == NULL) uLen = 0;
	else uLen = strlen(sServer);  
	if((uLen < 4)||(uLen > (DASCRED_SRV_SZ-1))){sWhich = sServer; }
	
	if(sRealm == NULL) uLen = 0;
	else uLen = strlen(sRealm);
	if((uLen < 4)||(uLen > (DASCRED_REALM_SZ-1))){sWhich = sRealm; }
	
	if(sHash == NULL) uLen = 0;
	else uLen = strlen(sHash);
	if((uLen < 2)||(uLen > (DASCRED_HASH_SZ-1))){sWhich = sHash; }
	
	/* Dataset string can be null */
	if(sDataset != NULL){ 
		uLen = strlen(sDataset);
		if((uLen < 2)||(uLen > (DASCRED_DSET_SZ-1))){sWhich = sDataset; }
	}
	
	if(sWhich){
		das_error(DASERR_CRED, "%s string is too large or too small");
		return false;
	}
	
	memset(pCred, 0, sizeof(das_credential));
	strncpy(pCred->sServer, sServer, DASCRED_SRV_SZ-1);
	strncpy(pCred->sRealm, sRealm, DASCRED_REALM_SZ-1);
	strncpy(pCred->sHash, sHash, DASCRED_HASH_SZ-1);
	if(sDataset != NULL) strncpy(pCred->sDataset, sDataset, DASCRED_DSET_SZ-1);
	
	pCred->bValid = true;  /* Assume it works until proven otherwise */
	
	return true;
}

/* ************************************************************************** */
DasCredMngr* new_CredMngr(const char* sKeyStore)
{
	/* I don't actually have the code to read/write key files at this point */
	if(sKeyStore != NULL){
		das_error(DASERR_NOTIMP, "Reading/Writing to keystore files is not yet "
		          "implemented.");
		return NULL;
	}
	
	DasCredMngr* pThis = (DasCredMngr*)calloc(1, sizeof(DasCredMngr));
	
	das_credential fill; 
	memset(&fill, 0, sizeof(das_credential));
	
	pThis->pCreds = new_DasAry(
		"cashed_credentials",vtUnknown, sizeof(das_credential), (const byte*)(&fill), 
		RANK_1(0), UNIT_DIMENSIONLESS
	);
	pThis->prompt = das_term_prompt;

	if(sKeyStore)
		strncpy(pThis->sKeyFile, sKeyStore, 127);

	return pThis;
}

void del_CredMngr(DasCredMngr* pThis){
	dec_DasAry(pThis->pCreds);
	free(pThis);
}

das_credential* _CredMngr_getCred(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, 
	const char* sDataset, bool bValidOnly
){
	das_credential* pCred = NULL;
	for(ptrdiff_t i = 0; i < DasAry_size(pThis->pCreds); ++i){
		pCred = (das_credential*)DasAry_getAt(pThis->pCreds, vtUnknown, IDX0(i));
		
		if((!pCred->bValid)&&(bValidOnly)) continue;
		
		if(sServer == NULL){ if(pCred->sServer[0] != '\0') continue; }
		else{  if(strcmp(pCred->sServer, sServer) != 0) continue;    }
		
		if(sRealm == NULL){ if(pCred->sRealm[0] != '\0') continue; }
		else{  if(strcmp(pCred->sRealm, sRealm) != 0) continue;    }
		
		/* Dataset is different, only worry about matching that if it was set */
		if(pCred->sDataset[0] != '\0'){
			if(sDataset != NULL){
				if(strcmp(pCred->sDataset, sDataset) != 0) continue;    
			}
		}
		
		return pCred;
	}
	return NULL;
}

int CredMngr_addCred(DasCredMngr* pThis, const das_credential* pCred)
{
	/* Store it either in the old spot, or if that doesn't exist, make a 
	 * new one */
	das_credential* pOld;
	/* fprintf(stderr, "Adding server: %s, realm: %s, dataset: %s, hash: %s", 
			  pCred->sServer, pCred->sRealm, pCred->sDataset, pCred->sHash); */
	
	pOld = _CredMngr_getCred(pThis, pCred->sServer, pCred->sRealm, pCred->sDataset, false);
	if(pOld == NULL)
		DasAry_append(pThis->pCreds, (const byte*)pCred, 1);
	else
		memcpy(pOld->sHash, pCred->sHash, DASCRED_HASH_SZ); /* Get terminating null */
	
	return DasAry_size(pThis->pCreds);
}

int CredMngr_addUserPass(
   DasCredMngr* pThis, const char* sServer, const char* sRealm, 
   const char* sDataset, const char* sUser, const char* sPass
){
	das_credential cred;
	memset(&cred, 0, sizeof(das_credential));

	char sBuf[DASCRED_HASH_SZ+2];  /* 2 bytes longer than das_credential.sHash
	                                  to detect long hash values */
	if(strchr(sUser, ':') != NULL){
		das_error(DASERR_CRED, "The user name cannot contain a colon, ':', character");
		return -1;  /* If the error handler allows for a return */
	}

	/* Hash it */
	snprintf(sBuf, DASCRED_HASH_SZ+1, "%s:%s", sUser, sPassword); /* 257 is not an error */
	size_t uLen;
	char* sHash = das_b64_encode((unsigned char*)sBuf, strlen(sBuf), &uLen);
	/*fprintf(stderr, "DEBUG: Print hash: %s, length %zu\n", sHash, uLen); */
	if(uLen > (DASCRED_HASH_SZ-1)){
		free(sHash);
		pThis->sLastAuthMsg[0] = '\0';
		das_error(DASERR_CRED, "Username and password are too large for the hash buffer");
		return -1;
	}

	if(! das_cred_init(sServer, sRealm, sDataset, sHash))
		return -1;  /* Function sets it's own error message */
	
	return CredMngr_addCred(pThis, &cred);
}


const char* CredMngr_getHttpAuth(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, const char* sDataset
){
	
	das_credential* pCred = _CredMngr_getCred(pThis, sServer, sRealm, sDataset, true);
	if(pCred) return pCred->sHash;
	
	char sUser[128];
	char sPassword[128];
	
	const char* sMsg = NULL;
	if(pThis->sLastAuthMsg[0] != '\0') sMsg = pThis->sLastAuthMsg;
	
	char sBuf[DASCRED_HASH_SZ+2];  /* 2 bytes longer than das_credential.sHash
	                                  to detect long hash values */
	while(true){

		/* So I either don't have a credential, or it's not valid.  Get a new
		 * hash */
		if( ! pThis->prompt(sServer, sRealm, sDataset, sMsg, sUser, sPassword) ) 
			return NULL;
		
		if(strchr(sUser, ':') != NULL){
			sMsg = "The user name cannot contain a colon, ':', character";
			continue;
		}
	
		/* Hash it */
		snprintf(sBuf, DASCRED_HASH_SZ+1, "%s:%s", sUser, sPassword); /* 257 is not an error */
		size_t uLen;
		char* sHash = das_b64_encode((unsigned char*)sBuf, strlen(sBuf), &uLen);
		/*fprintf(stderr, "DEBUG: Print hash: %s, length %zu\n", sHash, uLen); */
		if(uLen > (DASCRED_HASH_SZ-1)){
			free(sHash);
			pThis->sLastAuthMsg[0] = '\0';
			das_error(DASERR_CRED, "Base64 output buffer is too small, tell "
					    "das2C maintainers to fix the problem");
			return NULL;
		}
		
		/* Store it either in the old spot, or if that doesn't exist, make a 
		 * new one */
		pCred = _CredMngr_getCred(pThis, sServer, sRealm, sDataset, false);
		if(pCred == NULL){
			das_credential cred;
			memset(&cred, 0, sizeof(cred));
			if(sServer != NULL) strncpy(cred.sServer, sServer, 127);
			if(sRealm != NULL) strncpy(cred.sRealm, sRealm, 127);
			if(sDataset != NULL) strncpy(cred.sDataset, sDataset, 127);
			DasAry_append(pThis->pCreds, (const byte*) &cred, 1);
			pCred = (das_credential*)DasAry_getAt(pThis->pCreds, vtUnknown, IDX0(-1));
		}
		memcpy(pCred->sHash, sHash, uLen+1); /* Get terminating null */
		free(sHash);
		
		pCred->bValid = true;
		pThis->sLastAuthMsg[0] = '\0';
		return pCred->sHash;
	}
	
	/* pThis->sLastAuthMsg[0] = '\0'; */
	/* return NULL; */
}

void CredMngr_authFailed(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, 
	const char* sDataset, const char* sMsg
){
	das_credential* pCred = _CredMngr_getCred(pThis, sServer, sRealm, sDataset, false);
	if(pCred != NULL) pCred->bValid = false;
	
	if(sMsg != NULL)
		strncpy(pThis->sLastAuthMsg, sMsg, DASCMGR_MSG_SZ-1);
	
}

das_prompt CredMngr_setPrompt(DasCredMngr* pThis, das_prompt new_prompt){
	das_prompt old = pThis->prompt;
	pThis->prompt = new_prompt;
	return old;
}

/* TODO: Add openssh password symetric key protection to the credentials file */
int CredMngr_save(DasCredMngr* pThis, const char* sSymKey, const char* sFile)
{

	if(sSymKey != NULL){
		daslog_error("Symetric key encryption of the credentials file is not yet implemented.");
		return -1;
	}

	/* Write all the current credentials to the given filename */
	const char* sOut = (sFile == NULL) ? pThis->sKeyFile : sFile;
	
	if(sOut[0] == '\0'){
		daslog_error("Can't save.  No credentials file specified either here or"
			" in the constructor");
		return -1;
	}

	FILE* pOut = fopen(sOut, "wb");

	int nRet = 0;
	das_credential* pCred = NULL;
	for(ptrdiff_t i = 0; i < DasAry_size(pThis->pCreds); ++i){
		pCred = (das_credential*)DasAry_getAt(pThis->pCreds, vtUnknown, IDX0(i));
		if(!pCred->bValid) 
			continue;

		if(pCred->sDataset[0] != '\0')
			fprintf(pOut, 
				"%s|%s|dataset|%s|%s\n", pCred->sServer, pCred->sRealm, pCred->sDataset,
				pCred->sHash
			);
		else
			fprintf(pOut, 
				"%s|%s|||%s\n", pCred->sServer, pCred->sRealm, pCred->sHash
			);
		++nRet;
	}

	fclose(pOut);

	if((sFile != NULL)&&(sFile[0] != '\0')){
		memset(pThis->sKeyFile, 0, DASCMGR_FILE_SZ);
		strncpy(pThis->sKeyFile, sFile, DASCMGR_FILE_SZ-1);
	}

	return nRet;
}

/* TODO: Add openssh password symetric key protection to the credentials file */
int CredMngr_load(DasCredMngr* pThis, const char* sSymKey, const char* sFile)
{
	if(sSymKey != NULL){
		daslog_error("Symetric key encryption of the credentials file is not yet implemented.");
		return -1;
	}

	const char* sIn = (sFile == NULL) ? pThis->sKeyFile : sFile;

	if(sIn[0] == '\0'){
		daslog_error("Can't load.  No credentials file specified either here or"
			" in the constructor");
		return -1;
	}

	FILE* pIn = fopen(sIn, "rb");

	// Make an array to the side to hold loaded credentials
	das_credential fill;
	memset(&fill, 0, sizeof(das_credential));
	
	DasAry* pTmpCreds = new_DasAry(
		"temp_credentials",vtUnknown, sizeof(das_credential), (const byte*)(&fill), 
		RANK_1(0), UNIT_DIMENSIONLESS
	);

	int nCreds = 0;
	const size_t uLineLen = DASCRED_SRV_SZ+DASCRED_REALM_SZ+DASCRED_DSET_SZ+DASCRED_HASH_SZ+40;
	char aLine[
		DASCRED_SRV_SZ+DASCRED_REALM_SZ+DASCRED_DSET_SZ+DASCRED_HASH_SZ+40
	] = {'\0'};
	
	char* aBeg[5] = {NULL};
	char* aEnd[5] = {NULL}; // These point at terminating nulls
	char* pChar = NULL;

	das_credential cred;
	int nLine = 0;

	size_t iSection;	
	while(fgets(aLine, uLineLen, pIn)){
		++nLine;
		
		memset(aBeg, 0, 5*sizeof(void*));
		memset(aEnd, 0, 5*sizeof(void*));

		// Section begin and end are the same for empty sections
		aBeg[0] = aLine;
		aEnd[4] = aLine + strlen(aLine) + 1;
		iSection = 0;
		for(pChar = aLine; *pChar != '\0'; ++pChar){
			if(*pChar == '|'){
				aEnd[iSection] = pChar;
				*pChar = '\0';
				++iSection;
				if(iSection > 4) break;

				aBeg[iSection] = pChar+1;
			}
		}
		
		// Only lines with 4 pipes are valid credentials, anything else is text
		if(iSection != 4) continue;

		// Strip ends
		for(iSection = 0; iSection < 5; ++iSection){
			if(aBeg[iSection] == aEnd[iSection]) continue;

			pChar = aBeg[iSection];
			while((pChar < aEnd[iSection]) && ((*pChar == ' ')||(*pChar == '\t'))){
				++pChar;
				aBeg[iSection] += 1;
			}

			pChar = aEnd[iSection];
			while((pChar >= aBeg[iSection]) && ((*pChar == ' ')||(*pChar == '\t'))){
				--pChar;
				*(aEnd[iSection]) = '\0';
				aEnd[iSection] -= 1;
			}
		}

		// Required sections: 0 = server, 1 = Realm, 4 = Hash
		if((aBeg[0] == aEnd[0])||(aBeg[1] == aEnd[1])||(aBeg[4] == aEnd[4]))
			continue;
		
		// Expect the key 'dataset' if aEnd[2] is not null
		if((*(aEnd[2]) != '\0')&&(strcmp(aBeg[2], "dataset") != 0)){
			daslog_warn_v(
				"%s,%d: Hashes for specific datasets must indicate the key 'dataset'",
				sIn, nLine
			);
			continue;
		}
		
		if(das_cred_init(
			&cred, aBeg[0], aBeg[1], *(aBeg[3]) == '\0' ? NULL : aBeg[3], aBeg[4]
		)){
			daslog_warn_v("%s,%d: Could not parse credential", sIn, nLine);
			continue;
		}

		// Add the credential
		DasAry_append(pTmpCreds, (const byte*)(&cred), 1);
		++nCreds;
	}


	// Merge in the new credentials from the file
	if(nCreds > 0){
		das_credential* pNew = NULL;
		das_credential* pOld = NULL;
		for(ptrdiff_t i = 0; i < DasAry_size(pTmpCreds); ++i){
			pNew = (das_credential*)DasAry_getAt(pThis->pCreds, vtUnknown, IDX0(i));
			
			pOld = _CredMngr_getCred(pThis, pNew->sServer, pNew->sRealm, pNew->sDataset, false);
			if(pOld == NULL){
				DasAry_append(pThis->pCreds, (const byte*)pNew, 1); // append always copies
			}
			else{
				if(pOld->sHash && (strcmp(pOld->sHash, pNew->sHash) != 0))
					memcpy(pOld->sHash, pNew->sHash, DASCRED_HASH_SZ);
				else
					--nCreds;
			}
		}
	}

	dec_DasAry(pTmpCreds);  // Frees the temporary credentials array

	return nCreds;
}
