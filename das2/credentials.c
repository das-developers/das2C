/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "credentials.h"

/* More stack overflow copy-pasta.  This is getting embarrasing but get's
 * the job done. 
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

char* base64_encode(
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
	if((uLen < 4)||(uLen > 127)){sWhich = sServer; }
	
	if(sRealm == NULL) uLen = 0;
	else uLen = strlen(sRealm);
	if((uLen < 4)||(uLen > 127)){sWhich = sRealm; }
	
	if(sHash == NULL) uLen = 0;
	else uLen = strlen(sHash);
	if((uLen < 2)||(uLen > 255)){sWhich = sHash; }
	
	/* Dataset string can be null */
	if(sDataset != NULL){ 
		uLen = strlen(sDataset);
		if((uLen < 2)||(uLen > 255)){sWhich = sDataset; }
	}
	
	if(sWhich){
		das_error(DASERR_CRED, "%s string is too large or too small");
		return false;
	}
	
	memset(pCred, 0, sizeof(das_credential));
	strncpy(pCred->sServer, sServer, 127);
	strncpy(pCred->sRealm, sRealm, 127);
	strncpy(pCred->sHash, sHash, 255);
	if(sDataset != NULL) strncpy(pCred->sDataset, sDataset, 127);
	
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
	
	das_credential* pFill = (das_credential*)calloc(1, sizeof(das_credential));
	
	pThis->pCreds = new_DasAry(
		"cashed_credentials",vtUnknown, sizeof(DasCredMngr), (const byte*)pFill, 
		RANK_1(0), UNIT_DIMENSIONLESS
	);
	pThis->prompt = das_term_prompt;
	
	pThis->sKeyFile = sKeyStore;
	
	return pThis;
}

void del_DasCredMngr(DasCredMngr* pThis){
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
		memcpy(pOld->sHash, pCred->sHash, 256); /* Get terminating null */
	
	return DasAry_size(pThis->pCreds);
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
	
	char sBuf[258];  /* 258 is not an error, it's 2 bytes longer than 
	                  * das_credential.sHash to detect long hash values */
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
		snprintf(sBuf, 257, "%s:%s", sUser, sPassword); /* 257 is not an error */
		size_t uLen;
		char* sHash = base64_encode((unsigned char*)sBuf, strlen(sBuf), &uLen);
		/*fprintf(stderr, "DEBUG: Print hash: %s, length %zu\n", sHash, uLen); */
		if(uLen > 255){
			free(sHash);
			pThis->sLastAuthMsg[0] = '\0';
			das_error(DASERR_CRED, "Base64 output buffer is too small, tell "
					    "libdas2 maintainers to fix the problem");
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
	
	pThis->sLastAuthMsg[0] = '\0';
	return NULL;
}

void CredMngr_authFailed(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, 
	const char* sDataset, const char* sMsg
){
	das_credential* pCred = _CredMngr_getCred(pThis, sServer, sRealm, sDataset, false);
	if(pCred != NULL) pCred->bValid = false;
	
	if(sMsg != NULL)
		strncpy(pThis->sLastAuthMsg, sMsg, 1023);
	
}

das_prompt CredMngr_setPrompt(DasCredMngr* pThis, das_prompt new_prompt){
	das_prompt old = pThis->prompt;
	pThis->prompt = new_prompt;
	return old;
}

bool CredMngr_save(const DasCredMngr* pThis, const char* sFile){
	/* TODO */
	
	return false;
}
