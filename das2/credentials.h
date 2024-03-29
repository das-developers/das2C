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

#ifndef _das_credmngr_h_
#define _das_credmngr_h_

#include <stdio.h>

#include <das2/array.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file credentials.h Handle storing credentials during a Das2 session and 
 * optionally save them to a file */

/** @addtogroup IO
 * @{
 */

/** Encode provided binary data as base64 characters in a new buffer 
 * 
 * (Credit: stackoverflow user ryyst)
 * @param data A pointer to the data to encode
 * @param input_length The number of input bytes to encode
 * @param output_length a pointer to location to receive the encoded length
 * 
 * @returns A pointer to a newly allocated buffer of at least size
 *          output_length, or NULL if calloc failed.
 */
DAS_API char* das_b64_encode(
   const unsigned char* data, size_t input_length, size_t* output_length
);


/** Function signature for swapping out the user-prompt for credentials 
 * acquisition. 
 * 
 * @param sServer The server name
 * @param sRealm The authorization realm on this server, can be same as the dataset
 * @param sDataset The name of the dataset on this server
 * @param sMessage An additional message that may be supplied, such as 
 *        "The user name cannot contain a colon, ':', character"
 * @param sUser a pointer to 128 bytes of storage to hold the username
 * @param sPassword a pointer to 128 bytes of storage to hold the password
 * @returns true if the user entered a user name and password (even empty ones)
 *          and false if the prompt was canceled.
 */
typedef bool (*das_prompt)(
	const char* sServer, const char* sRealm, const char* sDataset, 
	const char* sMessage, char* sUser, char* sPassword
);

#define DASCRED_SRV_SZ    128
#define DASCRED_REALM_SZ  128
#define DASCRED_DSET_SZ   128
#define DASCRED_HASH_SZ   256

/** A single credential*/
typedef struct das_credential_t{
	bool bValid;
	char sServer[DASCRED_SRV_SZ];
	char sRealm[DASCRED_REALM_SZ];
	char sDataset[DASCRED_DSET_SZ];
	char sHash[DASCRED_HASH_SZ];
} das_credential;


/** Initialize a credential to be cached in the credentials manager
 * 
 * @param pCred A pointer to a das_credentials structure
 * 
 * @param sServer The name of the server, ex: 'jupiter.physics.uiowa.edu'
 * 
 * @param sRealm The authentication realm.  This is provided in the dsdf
 *                files under the securityRealm keyword.
 * 
 * @param sDataset The dataset, ex: 'Juno/WAV/Survey'  The dataset is typically
 *                 determined by the http module by URL inspection.  If this
 *                 credentials manager is used for a general URL then the 
 *                 http module will not specify the the dataset.  To match those
 *                 sites, use NULL here. 
 * 
 * @param sHash The hash value.  Currently the library only supports 
 *              HTTP Basic Authentication hashes. i.e. a USERNAME:PASSWORD
 *              string that has been base64 encoded.
 * 
 * @memberof das_credential
 */
DAS_API bool das_cred_init(
	das_credential* pCred, const char* sServer, const char* sRealm, 
	const char* sDataset, const char* sHash
);

#define DASCMGR_FILE_SZ 128
#define DASCMGR_MSG_SZ  1024

/** Credentials manager 
 * Handles a list of login credentials and supplies these as needed for network
 * operations
 */
typedef struct das_credmngr{
	DasAry* pCreds;
	das_prompt prompt;
	char sKeyFile[DASCMGR_FILE_SZ];
	char sLastAuthMsg[DASCMGR_MSG_SZ];
} DasCredMngr;

/** @} */
	
/** Initialize a new credentials manager, optionally from a saved list
 * 
 * @param sKeyStore If not NULL this saves the name of the intended
 *        credentials storage file.  It DOES NOT LOAD ANYTHING!  To load
 *        credentials use CredMngr_load(pThis, "myencryptkey").
 * 
 * @return A new credentials manager allocated on the heap
 * @memberof DasCredMngr
 */
DAS_API DasCredMngr* new_CredMngr(const char* sKeyStore);

/** Delete a credentials manager free'ing it's internal credential store
 * 
 * @param pThis A pointer to the credentials manager structure to free.  The
 *        pointer is no-longer valid after this call and should be set to NULL.
 * @memberof DasCredMngr
 */
DAS_API void del_CredMngr(DasCredMngr* pThis);

/** Manually add a credential to a credentials manager instead of prompting the
 * user.
 * 
 * Typically when the credentials manager does not have a password it needs it
 * calls the prompt function that was set using CredMngr_setPrompt() or the
 * built in command line prompter if no prompt function has been set.
 * 
 * @param pThis The credentials manager structure that will hold the new 
 *        credential in RAM
 * @param pCred The credential to add.  If an existing credential matches this
 *        one except for the hash value, the new hash will overwrite the old 
 *        one.
 * @return The new number of cached credentials
 */
DAS_API int CredMngr_addCred(DasCredMngr* pThis, const das_credential* pCred);


/** Get direct memory access to a stored credential 
 * 
 * Used by other functions to find a credential for a particular URL
 * 
 * @param pThis A credentials manager
 * @param sServer The service end point (A URL without fragments or query params)
 * @param sRealm The security realm
 * @param sDataset If not NULL, the dataset parameter must equal this
 * @param bValidOnly Only return valid credentials.  Credentials are assmed valid
 *        unless 
 * 
 * @returns A pointer to the in-memory credential, NULL if no credential matched
 * the given conditions
 */
DAS_API das_credential* CredMngr_getCred(
   DasCredMngr* pThis, const char* sServer, const char* sRealm, 
   const char* sDataset, bool bValidOnly
);

/** Manually add a credential to a credentials manager instead of prompting the
 * user.
 * 
 * This is a individual string version of CredMngr_addCred that calculates it's
 * own base64 hash.
 * 
 * @param pThis The credentials manager structure that will hold the new 
 *        credential in RAM
 * 
 * @param sServer The resource URL including the path, but not including 
 *                fragments or query parameters
 * @param sRealm  The security realm to which this credential shoud be supplied
 * @param sDataset If not NULL, the value of the 'dataset=' parameter that 
 *                must be present for this credential to apply
 * @param sUser   A user name
 * @param sPass   A plain-text password
 * @returns The new number of cached credentials, or -1 on an error
 */
DAS_API int CredMngr_addUserPass(
   DasCredMngr* pThis, const char* sServer, const char* sRealm, 
   const char* sDataset, const char* sUser, const char* sPass
);

/** Retrieve an HTTP basic authentication token for a given dataset on a given
 * server.  
 * 
 * Side Effect:
 *    This may call the .prompt() method, which may initiate Terminal IO.
 * 
 * @param pThis A pointer to a credentials manager structure
 * @param sServer The name of the server for which these credentials apply
 * @param sRealm A string identifing the system the user will be authenticating too.
 * @param sDataset The name of the dataset for which these credentials apply
 * @return The auth token, NULL if no auth token could be supplied 
 * @memberof DasCredMngr
 */
DAS_API const char* CredMngr_getHttpAuth(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, const char* sDataset
);

/** Let the credentials manager know that a particular authorization method 
 * failed.
 * 
 * The credentials manager can use this information to re-prompt the user if
 * desired
 * 
 * @param pThis A pointer to a credentials manager structure
 * @param sServer The name of the server for which these credentials apply
 * @param sRealm A string identifing the system the user will be authenticating too.
 * @param sDataset The name of the dataset for which these credentials apply
 * @param sMsg an optional message providing more details on why authentication
 *        failed
 * @memberof DasCredMngr
 */
DAS_API void CredMngr_authFailed(
	DasCredMngr* pThis, const char* sServer, const char* sRealm, 
	const char* sDataset, const char* sMsg
);


/** Change the function used to prompt users for das2 server credentials 
 * 
 * The built-in password prompt function assumes a console application, it
 * asks for a username then tries to set the controlling terminal to non-echoing
 * I/O and asks for a password.  It never returns false, so authentication is
 * endless cycle.  For long running programs a different function should be
 * supplied here.
 * 
 * @param pThis a pointer to a credentials manager structure
 * @param new_prompt The new function, or NULL if no password prompt should 
 *        ever be issued
 * @return The old password prompt function
 * @memberof DasCredMngr
 */
DAS_API das_prompt CredMngr_setPrompt(DasCredMngr* pThis, das_prompt new_prompt);

/** Save the current credentials to the given filename
 * 
 * NOTE: The credentials file is not encrypted. It could be since openssl is
 *       a required dependency of das2C, but the functionality to do so hasn't
 *       been implemented
 * 
 * @param pThis a pointer to a CredMngr structure
 * 
 * @param sSymKey A key to use for encrypting the credentials file
 *          (Not yet implemented, added for stable ABI, use NULL here)
 * 
 * @param sFile the file to hold the loosly encypted credentials.  If NULL
 *        then the keyfile indicated in the constructor, new_CredMngr() is
 *        used.  If the file does not exist it is created.
 * 
 * @returns The number of credential rows saved, or -1 on an error.
 * @memberof DasCredMngr
 */
DAS_API int CredMngr_save(DasCredMngr* pThis, const char* sSymKey, const char* sFile);

/** Merge in credentials from the given filename
 *
 * NOTE: The credentials file is not encrypted. It could be since openssl is
 *       a required dependency of das2C, but the functionality to do so hasn't
 *       been implemented
 *
 * @param pThis a pointer to a CredMngr structure
 *
 * @param sSymKey A key to use for encrypting the credentials file
 *          (Not yet implemented, added for stable ABI, use NULL here)
 * 
 * @param sFile the file to hold the loosly encypted credentials.  If the 
 *        file does not exist, 0 is returned.  Thus a missing credentials file
 *        is not considered an error.  If sFile is NULL, then the keyfile
 *        given in the constructor, new_CredMngr() is used.
 * 
 * @returns The number of NEW credential sets and conditions. Thus loading
 *        the exact same file twice should return 0 on the second load.
 * 
 * @memberof DasCredMngr
 */
DAS_API int CredMngr_load(DasCredMngr* pThis, const char* sSymKey, const char* sFile);


#ifdef __cplusplus
}
#endif

#endif /* _das_credmngr_h_ */

