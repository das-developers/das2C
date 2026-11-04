/* Copyright (C) 2017-2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

/** @file creds.h
 * Logins for das servers, see DasCredMngr
 */

#ifndef _das_creds_h_
#define _das_creds_h_

#include <das3/datum.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup IO
 * @{
 */

/* ************************************************************************* */
/* Sizes and return values */

/* The largest strings this version holds, terminator included.  Buffers this
 * size never come back short.  Later versions may hold more; a caller that
 * sized to these then gets DASCRED_SHORT, never an overrun. */
#define DASCRED_SRV_SZ     256  /**< Server, a URL without query or fragment */
#define DASCRED_REALM_SZ   128  /**< Authentication realm */
#define DASCRED_DSET_SZ    128  /**< das2 "dataset=" value */
#define DASCRED_SECRET_SZ 8192  /**< Secret, about the largest HTTP header */
#define DASCRED_USER_SZ    128  /**< User name from a prompt */
#define DASCRED_PASS_SZ    128  /**< Password from a prompt */
#define DASCRED_STORE_SZ   512  /**< Key file path */

/** A credential was found, but a buffer was too small for one of its strings.
 * What fit is copied and terminated. */
#define DASCRED_SHORT -1

/** No credential matched, or one has no secret.  Not an error, nothing is
 * logged. */
#define DASCRED_NONE  -2

/* ************************************************************************* */
/* Credentials */

/** How a secret is presented to a server */
typedef enum das_auth_type {
	DASAUTH_NONE,    /**< No secret is set */
	DASAUTH_BASIC,   /**< HTTP Basic, the secret is base64 of "user:password" */
	DASAUTH_BEARER   /**< A bearer token, as from an OAuth 2.0 login */
} das_auth_e;

/** The server refused this secret (HTTP 401).  A rejected secret is never sent
 * again on its own; a new one from DasCredMngr_setSecret() clears this. */
#define DASCRED_REJECTED 0x00000001

/** The secret's expiration time has passed.  Worked out when the record is
 * copied out, never stored. */
#define DASCRED_EXPIRED  0x01000000

/** A copy of one credential record, made for the caller
 *
 * This is how the manager reports a credential.  It never carries the secret
 * itself, see DasCredMngr_getSecret() for that.  The caller owns the string
 * buffers and says where they are and how big they are, usually with
 * das_cred_init(); the manager only copies into them.  A buffer pointer of
 * NULL with a size of 0 means that string is not wanted.
 *
 * @code
 *   char sServer[DASCRED_SRV_SZ];
 *   char sRealm[DASCRED_REALM_SZ];
 *   das_cred cred;
 *   das_cred_init(&cred, sServer, sizeof(sServer), sRealm, sizeof(sRealm),
 *                 NULL, 0);
 * @endcode
 *
 * The flags are DASCRED_REJECTED and DASCRED_EXPIRED.  The low 24 bits are
 * kept with the credential, the high 8 are worked out when it is copied.
 */
typedef struct das_cred {
	int16_t    nCred;      /**< The manager's ID for it, > 0 when filled */
	uint32_t   uFlags;     /**< DASCRED_REJECTED, DASCRED_EXPIRED */
	das_auth_e eScheme;    /**< DASAUTH_NONE if it has no secret */
	das_datum  expires;    /**< vtLong UNIT_T1970, a null datum for never */
	char*  sServer;        /**< Caller's buffer for the server, or NULL */
	size_t uServerSz;      /**< Size of sServer in bytes */
	char*  sRealm;         /**< Caller's buffer for the realm, or NULL */
	size_t uRealmSz;       /**< Size of sRealm in bytes */
	char*  sDataset;       /**< Caller's buffer for the dataset, or NULL */
	size_t uDatasetSz;     /**< Size of sDataset in bytes */
} das_cred;

/** Point a credential record at the caller's buffers
 *
 * Everything else in the record is cleared and each buffer given is set to
 * an empty string.
 *
 * @param pCred The record
 * @param sServer A buffer for the server, or NULL
 * @param uServerSz Its size in bytes, DASCRED_SRV_SZ is never short
 * @param sRealm A buffer for the realm, or NULL
 * @param uRealmSz Its size in bytes, DASCRED_REALM_SZ is never short
 * @param sDataset A buffer for the dataset, or NULL
 * @param uDatasetSz Its size in bytes, DASCRED_DSET_SZ is never short
 * @memberof das_cred
 */
DAS_API void das_cred_init(
	das_cred* pCred, char* sServer, size_t uServerSz, char* sRealm,
	size_t uRealmSz, char* sDataset, size_t uDatasetSz
);

/* ************************************************************************* */
/* Prompts */

/** A function that asks a person for a user name and password
 *
 * The manager calls one when a server wants a login it does not have, or
 * refused the one it sent.  The built-in one is das_term_prompt(); a program
 * with a window should supply its own.
 *
 * @param pUser    Whatever was given to DasCredMngr_setPrompt()
 * @param sServer  The server asking
 * @param sRealm   The realm it named, or NULL
 * @param sDataset The das2 dataset, or NULL
 * @param sMessage Why the user is being asked again, or NULL; for example
 *                 "Credentials not accepted by remote server"
 * @param sUser    Where to put the user name
 * @param uUserSz  The size of sUser, at least DASCRED_USER_SZ
 * @param sPass    Where to put the password
 * @param uPassSz  The size of sPass, at least DASCRED_PASS_SZ
 * @returns true if the user gave a login, even an empty one, false if they
 *          declined
 */
typedef bool (*das_prompt)(
	void* pUser, const char* sServer, const char* sRealm, const char* sDataset,
	const char* sMessage, char* sUser, size_t uUserSz, char* sPass,
	size_t uPassSz
);

/** The built-in prompt, for programs run from a terminal
 *
 * Asks on the terminal itself, not stdin, which may be carrying a stream.
 * The password is not echoed.  It declines, so no login is sent, when the
 * program has no terminal, is not in the foreground, reaches end of input or
 * is given a blank user name.  pUser is not used.
 *
 * A program started with a hidden console would wait at a prompt no one can
 * see, so programs with windows should supply their own.
 */
DAS_API bool das_term_prompt(
	void* pUser, const char* sServer, const char* sRealm, const char* sDataset,
	const char* sMessage, char* sUser, size_t uUserSz, char* sPass,
	size_t uPassSz
);

/* ************************************************************************* */
/* The manager */

/** @class DasCredMngr creds.h <das3/creds.h>
 *
 * Holds the logins for a session and supplies them as servers ask.
 *
 * Most programs only need four calls: make a manager, point it at a key
 * file, choose how to ask the user for a login, and hand it to the HTTP
 * functions.  The manager supplies credentials when a server asks for them,
 * prompts when it has none, and remembers what worked.
 *
 * @code
 *   DasCredMngr* pMgr = new_DasCredMngr();
 *   DasCredMngr_setFileStore(pMgr, sKeyFile);
 *   DasCredMngr_load(pMgr);
 *   DasCredMngr_setPrompt(pMgr, das_term_prompt, NULL);
 *
 *   DasHttpResp res;
 *   if(das_http_getBody(sUrl, "my_prog", pMgr, &res, 6.0))
 *       ...
 *
 *   DasCredMngr_save(pMgr);
 *   del_DasCredMngr(pMgr);
 * @endcode
 *
 * Nothing happens unless asked for.  A new manager reads no files and
 * prompts no one; it is an empty list in memory until it is given a store
 * and a prompt.
 *
 * @par Credentials
 *
 * Each credential is a record saying where it applies (a server, an optional
 * realm, an optional das2 dataset) and one secret for it.  Records and
 * secrets are set separately: DasCredMngr_addCred() finds or makes the
 * record, DasCredMngr_setSecret() or DasCredMngr_setUserPass() gives it a
 * secret.
 *
 * @code
 *   int16_t nCred = DasCredMngr_addCred(pMgr, sUrl, "My Realm", NULL);
 *   if(nCred > 0)
 *       DasCredMngr_setUserPass(pMgr, nCred, sUser, sPass);
 * @endcode
 *
 * The number that comes back, nCred, is the manager's ID for the record.  It
 * is never 0, never reused by that manager, and means nothing to another
 * manager or another run, so do not save it.  A function given an ID for a
 * record that has been removed finds nothing.
 *
 * @par Matching
 *
 * The server is a URL without its query or fragment.  Old das2 servers name
 * the data source in a "dataset=" query value instead of the path, so for
 * those the dataset is part of the match:
 *
 *   | URL                                                    | server                 | dataset         |
 *   |--------------------------------------------------------|------------------------|-----------------|
 *   | https://host/das/stream?server=dataset&dataset=Juno/Wav/Survey | https://host/das/stream | Juno/Wav/Survey |
 *   | https://host/das/stream/source/juno/wav/survey/flex    | (the whole URL)        |                 |
 *
 * Those two are separate records even when one login works for both.  A
 * record for a specific dataset is preferred over one for the whole server.
 * These are the rules of this version; logins that work across servers, as
 * OAuth 2.0 tokens can, may widen them.
 *
 * @par Storage
 *
 * The manager keeps credentials between runs only when given a store.  The
 * one store today is a key file, see DasCredMngr_setFileStore().  It is plain
 * text, protected the way ~/.netrc and ssh keys are, by being readable only
 * by its owner, not by encryption.  A key file that other users can read or
 * change is refused.  Use these functions to change it rather than editing it
 * by hand.
 *
 * A manager is not safe to use from two threads at once.  Two processes may
 * share one key file.
 */
typedef struct das_credmngr DasCredMngr;

/** Make an empty credentials manager
 *
 * It has no credentials, no store and no prompt.
 *
 * @returns A new manager, or NULL if memory ran out
 * @memberof DasCredMngr
 */
DAS_API DasCredMngr* new_DasCredMngr(void);

/** Free a credentials manager
 *
 * Secrets are wiped from memory first.  Nothing is saved; call
 * DasCredMngr_save() before this to keep changes.
 * @memberof DasCredMngr
 */
DAS_API void del_DasCredMngr(DasCredMngr* pThis);

/** Find or make the record for a server, realm and dataset
 *
 * Matching here is exact: a NULL dataset finds or makes the record for the
 * whole server, never one for a particular dataset.  NULL and "" mean the
 * same thing for realm and dataset.
 *
 * @param pThis The manager
 * @param sServer The server, a URL without query or fragment
 * @param sRealm The realm, or NULL for any realm on that server.  Basic logins
 *        always have one; bearer token servers may not.
 * @param sDataset The das2 "dataset=" value, or NULL
 * @returns The record's ID, > 0, or -1 if a string is too long or the
 *        manager is full
 * @memberof DasCredMngr
 */
DAS_API int16_t DasCredMngr_addCred(
	DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset
);

/** Give a record its secret, replacing any it had
 *
 * The secret is stored as given and is what gets sent: for DASAUTH_BASIC the
 * base64 of "user:password" (DasCredMngr_setUserPass() makes that), for
 * DASAUTH_BEARER the token.  Setting a secret clears DASCRED_REJECTED.
 *
 * @param pThis The manager
 * @param nCred The record's ID
 * @param eScheme How the secret is sent.  DASAUTH_NONE removes the secret
 *        and keeps the record.
 * @param sSecret The secret, copied.  Ignored for DASAUTH_NONE.
 * @param pExpires When it stops working, in any time units, or NULL or a null
 *        datum for never
 * @returns DAS_OKAY, DASCRED_NONE for an unknown ID, or a positive error
 *        code for a secret that is too long or not a time
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_setSecret(
	DasCredMngr* pThis, int16_t nCred, das_auth_e eScheme, const char* sSecret,
	const das_datum* pExpires
);

/** Give a record an HTTP Basic login from a user name and password
 *
 * Makes the DASAUTH_BASIC secret and calls DasCredMngr_setSecret() with it.
 * It never expires.
 *
 * @param pThis The manager
 * @param nCred The record's ID
 * @param sUser The user name.  It may not contain a colon (RFC 7617).
 * @param sPass The password
 * @returns DAS_OKAY, DASCRED_NONE for an unknown ID, or a positive error
 *        code for a bad user name or a login that is too long
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_setUserPass(
	DasCredMngr* pThis, int16_t nCred, const char* sUser, const char* sPass
);

/** Forget a credential
 *
 * The record and its secret go now, and from the store at the next
 * DasCredMngr_save().
 *
 * @returns DAS_OKAY, or DASCRED_NONE for an unknown ID
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_removeCred(DasCredMngr* pThis, int16_t nCred);

/** Find the credential for a server, realm and dataset
 *
 * A record for this dataset is preferred, then one for the whole server.
 *
 * @code
 *   char sRealm[DASCRED_REALM_SZ];
 *   das_cred cred;
 *   das_cred_init(&cred, NULL, 0, sRealm, sizeof(sRealm), NULL, 0);
 *
 *   if(DasCredMngr_getCred(pMgr, sUrl, NULL, NULL, true, &cred) == DAS_OKAY){
 *       char sTok[DASCRED_SECRET_SZ];
 *       DasCredMngr_getSecret(pMgr, cred.nCred, sTok, sizeof(sTok));
 *       ...
 *   }
 * @endcode
 *
 * @param pThis The manager
 * @param sServer The server, a URL without query or fragment
 * @param sRealm The realm, or NULL for any
 * @param sDataset The das2 "dataset=" value, or NULL
 * @param bValidOnly If true, skip credentials with no secret, or one that was
 *        rejected or has expired
 * @param pOut The record to fill in, see das_cred_init()
 * @returns DAS_OKAY, DASCRED_SHORT if a buffer in pOut was too small,
 *        DASCRED_NONE if nothing matched, or a positive error code for bad
 *        arguments
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_getCred(
	const DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, bool bValidOnly, das_cred* pOut
);

/** Copy out a credential's secret
 *
 * Works like snprintf: at most uLen - 1 bytes and a terminator are written,
 * and the whole length is returned, so a return of uLen or more means the
 * buffer was too small.  sBuf may be NULL when uLen is 0, to ask the length.
 *
 * The copy is the caller's to protect.  Clear it when done, and don't log it.
 *
 * @param pThis The manager
 * @param nCred The record's ID
 * @param sBuf Where to put the secret
 * @param uLen The size of sBuf, DASCRED_SECRET_SZ is never short
 * @returns The secret's length, 0 if the record has none, or DASCRED_NONE
 *        for an unknown ID
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_getSecret(
	const DasCredMngr* pThis, int16_t nCred, char* sBuf, size_t uLen
);

/** The number of credentials held
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_numCreds(const DasCredMngr* pThis);

/** Copy out a credential by position, for listing them
 *
 * Positions run from 0 to DasCredMngr_numCreds() - 1.  They are for walking
 * the list once; adding, removing or loading may reorder them.  Keep nCred
 * to refer to a credential later.
 *
 * @param pThis The manager
 * @param i The position
 * @param pOut The record to fill in, see das_cred_init()
 * @returns DAS_OKAY, DASCRED_SHORT if a buffer in pOut was too small, or
 *        DASCRED_NONE if i is out of range
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_getCredByIdx(
	const DasCredMngr* pThis, int i, das_cred* pOut
);

/** Choose how the manager asks for a login
 *
 * @param pThis The manager
 * @param fPrompt The prompt, das_term_prompt() for a terminal program, or
 *        NULL never to ask
 * @param pUser Handed to every call of fPrompt
 * @returns The prompt it replaces
 * @memberof DasCredMngr
 */
DAS_API das_prompt DasCredMngr_setPrompt(
	DasCredMngr* pThis, das_prompt fPrompt, void* pUser
);

/** The prompt in use, NULL if none
 *
 * @param pThis The manager
 * @param ppUser If not NULL, gets the pUser given with the prompt
 * @memberof DasCredMngr
 */
DAS_API das_prompt DasCredMngr_getPrompt(
	const DasCredMngr* pThis, void** ppUser
);

/* ************************************************************************* */
/* Stores */

/** Keep credentials in a key file
 *
 * This only says where; nothing is read or written until
 * DasCredMngr_load() or DasCredMngr_save().  Credentials already held stay,
 * so loading from one store, switching, and saving moves them.
 *
 * The file is made readable only by its owner.  It need not exist yet.
 *
 * @param pThis The manager
 * @param sPath The file, or NULL to have no store
 * @returns DAS_OKAY, or a positive error code for a path longer than
 *        DASCRED_STORE_SZ - 1
 * @memberof DasCredMngr
 */
DAS_API DasErrCode DasCredMngr_setFileStore(
	DasCredMngr* pThis, const char* sPath
);

/** Where credentials are kept, for messages
 *
 * @returns "file:" and the path for a key file, or "" for no store.  The part
 *        before the colon names the kind of store.
 * @memberof DasCredMngr
 */
DAS_API const char* DasCredMngr_store(const DasCredMngr* pThis);

/** Merge credentials from the store into memory
 *
 * Where a record is in both, the store's secret wins.  A credential this
 * manager got from the store and has not changed since, which the store no
 * longer has, was removed by another process and is removed here too.
 *
 * A key file that does not exist yet holds nothing, which is not an error.
 * One that other users can read or change, or that belongs to someone else,
 * is refused, as ssh refuses such keys; the error says how to fix it.
 *
 * @returns The number of credentials added or changed, or -1 if there is no
 *        store or it could not be read
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_load(DasCredMngr* pThis);

/** Write the credentials to the store
 *
 * The store is read again first and merged, so processes sharing a key file
 * keep each other's changes.  This manager's changes since it last loaded or
 * saved win, including removals; for everything else the store's version is
 * kept, and taken into memory as DasCredMngr_load() would.  Rejected and
 * expired credentials are saved too, with their flags.  A key file is
 * replaced whole, so a crash part way through leaves the old one, and is
 * refused for the same reasons as in DasCredMngr_load().
 *
 * @returns The number of credentials in the store afterward, or -1 if there
 *        is no store or it could not be written
 * @memberof DasCredMngr
 */
DAS_API int DasCredMngr_save(DasCredMngr* pThis);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_creds_h_ */
