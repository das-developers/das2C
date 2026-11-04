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

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "util.h"
#include "array.h"
#include "log.h"
#include "time.h"
#include "units.h"

#include "creds_p.h"

/* OAuth 2.0 and other future work, notes for whoever picks it up
 * ===============================================================
 *
 * Today the manager hands out HTTP Basic logins and bearer tokens someone
 * pasted in.  The shape was chosen so OAuth 2.0 can be added without breaking
 * the public API.  How the pieces are meant to fit:
 *
 * Records and secrets.  A record says where a login applies (server, realm,
 * das2 dataset).  A secret is what gets sent, with its scheme, expiration and
 * flags.  An access token is a secret like any other; it expires and is
 * replaced, the record stays.  A refresh token is a second secret for the
 * same record.  Add it as a second row index in cred_rec and a
 * DasCredMngr_setRefresh(); the key file grows a "refresh" key whose value
 * carries its own [XXXXXX] prefix.  The public das_cred needs no new field:
 * a DASCRED_REFRESHABLE flag bit says one exists.
 *
 * Where refresh happens.  _DasCredMngr_getHttpAuth() is private (creds_p.h)
 * so it can grow this freely.  When the access token is expired and a
 * refresh token exists it POSTs to the token endpoint, sets the new access
 * token, and must save at once: RFC 9700 has public clients rotate refresh
 * tokens, so the old one dies on use.  An unsaved new one means the next run
 * logs in again; two processes refreshing with the same token look like a
 * replay and the server revokes both.  So refresh runs inside _DasCredMngr_sync()
 * under the exclusive sidecar lock: lock, re-read, refresh only if the file
 * still holds the token we were going to use, write, unlock.  The write
 * through is unconditional, no option to turn it off.
 *
 * Asking the user.  das_prompt is for user names and passwords only, HTTP
 * Basic.  OAuth on a terminal is the device flow (RFC 8628): show a URL and a
 * code, poll the token endpoint.  That is a different conversation and gets
 * its own callback and setter, with a default that prints to stderr like
 * das_term_prompt().  Browser login (auth code + PKCE, RFC 8252 loopback) is
 * an application hook, the library should not open browsers.  Never add the
 * OAuth password grant; RFC 9700 says MUST NOT.  Today a 401 always leads to
 * the Basic prompt; http.c should pass the WWW-Authenticate scheme in so a
 * Bearer challenge does not ask for a password.
 *
 * Finding the login server.  RFC 9728 lets a 401 point at
 * /.well-known/oauth-protected-resource, which names authorization servers;
 * RFC 8414 metadata there gives the token and device endpoints.  Discovery is
 * also a phishing path: a hostile server can name a hostile login page.  So
 * only issuers the application configured are used, addIssuer(sIssuer,
 * sClientId, sScope) being both client setup and the trust list.  Check that
 * the metadata's "resource" and "issuer" values match the URLs they came
 * from, show the issuer in the prompt, and ask for tokens bound to the das
 * server (RFC 8707 resource indicators).
 *
 * Matching.  Basic logins are per endpoint, tokens belong to an issuer and
 * may work at many hosts (Earthdata Login across DAACs).  The header calls
 * exact server matching the rule of this version, not a promise, so issuer
 * scoped records can widen it later.
 *
 * Sender constrained tokens.  DPoP (RFC 9449) needs a second header signed
 * over the method and URL.  Since getHttpAuth is private, change it to emit
 * whole header lines when that comes, and give it the method.
 *
 * Rejected secrets.  A 401 sets DASCRED_REJECTED and the secret is never sent
 * on its own again, across processes too since the flag is saved.  Many
 * institutional logins lock an account after a few failures, an unattended
 * job resending a known bad password would do that.  Only 401 rejects.
 *
 * Memory.  Secrets are wiped when replaced or removed, the secrets array is
 * D2ARY_WIPE so every block it frees is zeroed, including the old one each
 * time it grows, and stack buffers that held one are wiped before return.
 * The terminal prompt reads unbuffered so stdio keeps no copy.  http.c's
 * request DasBuf holds the Authorization header and is not wiped yet.
 *
 * Other stores.  Keychain, DPAPI and libsecret come as set<Place>Store()
 * setters, or one setHookStore(fLoad, fSave, pUser) so applications plug in
 * their own without das2C linking any of them.  _DasCredMngr_sync() is the seam.
 */

/* ************************************************************************* */
/* Terminal prompt */

/* Results of _termReadline() */
#define TERM_LINE_OK   0
#define TERM_NO_LINE   1   /* no terminal, not in the foreground, or end of input */
#define TERM_TOO_LONG  2

/* Read one line typed at the terminal into sBuf as UTF-8, without echo when
 * bEcho is false.  The line ending is dropped.
 *
 * This reads the terminal itself (CONIN$, /dev/tty), not stdin, which may be
 * redirected or carrying a das stream.  A GUI program may have no console at
 * all, then this fails at once.  One started with a hidden console waits for
 * input nobody can see, which no check here can tell apart from Windows
 * Terminal, so GUI hosts should set their own prompt (GitHub issue #3).
 */
#ifdef _WIN32

static bool _termUsable(void)
{
	HANDLE hIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL
	);
	if(hIn == INVALID_HANDLE_VALUE) return false;
	CloseHandle(hIn);
	return true;
}

static int _termReadline(char* sBuf, size_t uLen, bool bEcho)
{
	HANDLE hIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL
	);
	if(hIn == INVALID_HANDLE_VALUE) return TERM_NO_LINE;

	DWORD uMode = 0;
	bool bRestore = false;
	if(!bEcho && GetConsoleMode(hIn, &uMode))
		bRestore = SetConsoleMode(hIn, uMode & ~ENABLE_ECHO_INPUT);

	/* UTF-16 from the console, so its code page doesn't matter */
	wchar_t wsLine[256];
	DWORD uRead = 0;
	bool bRead = ReadConsoleW(hIn, wsLine, 255, &uRead, NULL) && (uRead > 0);
	bool bWhole = bRead && (wsLine[uRead - 1] == L'\n');

	/* Too long, drop the rest of the line so it isn't read as the next one */
	if(bRead && !bWhole){
		wchar_t wsRest[64];
		DWORD uRest = 0;
		while(ReadConsoleW(hIn, wsRest, 64, &uRest, NULL) && (uRest > 0)){
			if(wsRest[uRest - 1] == L'\n') break;
		}
		_dascred_wipe(wsRest, sizeof(wsRest));
	}

	if(bRestore) SetConsoleMode(hIn, uMode);
	CloseHandle(hIn);

	int nRet = TERM_LINE_OK;
	if(!bRead) nRet = TERM_NO_LINE;
	else if(!bWhole) nRet = TERM_TOO_LONG;
	else{
		while((uRead > 0) && ((wsLine[uRead-1] == L'\n')||(wsLine[uRead-1] == L'\r')))
			--uRead;

		/* Ctrl+Z is end of input on the Windows console */
		if((uRead > 0) && (wsLine[0] == 0x1A)) nRet = TERM_NO_LINE;
		else{
			int nLen = 0;
			if(uRead > 0){
				nLen = WideCharToMultiByte(
					CP_UTF8, 0, wsLine, (int)uRead, sBuf, (int)uLen - 1, NULL, NULL
				);
				if(nLen == 0) nRet = TERM_TOO_LONG;
			}
			sBuf[nLen] = '\0';
		}
	}
	_dascred_wipe(wsLine, sizeof(wsLine));  /* may hold the password */
	return nRet;
}

#else

/* A background job that reads its terminal is stopped by SIGTTIN and sits
 * there saying nothing, so only the foreground job may prompt. */
static bool _termForeground(int nFd)
{
	pid_t nGrp = tcgetpgrp(nFd);
	return (nGrp != -1) && (nGrp == getpgrp());
}

static bool _termUsable(void)
{
	int nFd = open("/dev/tty", O_RDONLY);
	if(nFd < 0) return false;
	bool bOkay = _termForeground(nFd);
	close(nFd);
	return bOkay;
}

static int _termReadline(char* sBuf, size_t uLen, bool bEcho)
{
	FILE* pTty = fopen("/dev/tty", "r");
	if(pTty == NULL) return TERM_NO_LINE;

	/* Unbuffered, so stdio holds no copy of a password in its own buffer */
	setvbuf(pTty, NULL, _IONBF, 0);

	int nFd = fileno(pTty);
	if(!_termForeground(nFd)){
		fclose(pTty);
		return TERM_NO_LINE;
	}

	struct termios old, quiet;
	bool bRestore = false;
	if(!bEcho && (tcgetattr(nFd, &old) == 0)){
		quiet = old;
		quiet.c_lflag &= ~ECHO;
		bRestore = (tcsetattr(nFd, TCSAFLUSH, &quiet) == 0);
	}

	bool bRead = (fgets(sBuf, (int)uLen, pTty) != NULL);
	size_t n = bRead ? strlen(sBuf) : 0;
	bool bWhole = (n > 0) && (sBuf[n-1] == '\n');

	/* A full buffer: the line fit exactly, or it's too long.  If too long,
	   drop the rest of it so it isn't read as the next line. */
	if(bRead && !bWhole && (n == uLen - 1)){
		int c = fgetc(pTty);
		if(c == '\n')
			bWhole = true;
		else
			while((c != EOF) && (c != '\n')) c = fgetc(pTty);
	}

	if(bRestore) tcsetattr(nFd, TCSAFLUSH, &old);
	fclose(pTty);

	/* A partial line followed by end of input is no line */
	if(!bRead || (!bWhole && (n < uLen - 1))) return TERM_NO_LINE;
	if(!bWhole) return TERM_TOO_LONG;

	while((n > 0) && ((sBuf[n-1] == '\n')||(sBuf[n-1] == '\r')))
		sBuf[--n] = '\0';
	return TERM_LINE_OK;
}

#endif

bool das_term_prompt(
	void* pUser, const char* sServer, const char* sRealm, const char* sDataset,
	const char* sMessage, char* sUser, size_t uUserSz, char* sPass,
	size_t uPassSz
){
	(void)pUser;

	if(!_termUsable()){
		fprintf(stderr, "Authentication required but there is no terminal in the "
		        "foreground to ask on, not authenticating\n");
		return false;
	}

	fprintf(stderr, "Authentication Required\n");
	if(sMessage)
		fprintf(stderr, "NOTE:    %s\n", sMessage);
	if(sServer != NULL)  fprintf(stderr, "   Server:  %s\n", sServer);
	if((sRealm != NULL)&&(sRealm[0] != '\0'))     fprintf(stderr, "   Realm:   %s\n", sRealm);
	if((sDataset != NULL)&&(sDataset[0] != '\0')) fprintf(stderr, "   Dataset: %s\n", sDataset);

	int nRet;
	while(true){
		fprintf(stderr, "Login Name (blank to cancel) > ");
		nRet = _termReadline(sUser, uUserSz, true);
		if(nRet != TERM_TOO_LONG) break;
		fprintf(stderr, "That login name is too long, the limit is %zu bytes\n", uUserSz - 1);
	}
	if(nRet == TERM_NO_LINE){
		fprintf(stderr, "\nNo login read, end of input, not authenticating\n");
		return false;
	}
	if(sUser[0] == '\0') return false;

	while(true){
		fprintf(stderr, "Password > ");
		nRet = _termReadline(sPass, uPassSz, false);
		fprintf(stderr, "\n");  /* The Enter key wasn't echoed */
		if(nRet != TERM_TOO_LONG) break;
		fprintf(stderr, "That password is too long, the limit is %zu bytes\n", uPassSz - 1);
	}
	return (nRet == TERM_LINE_OK);
}

/* ************************************************************************* */
/* Wiping */

void _dascred_wipe(void* pMem, size_t uLen)
{
	if(pMem == NULL) return;
#ifdef _WIN32
	SecureZeroMemory(pMem, uLen);
#else
	/* Writes through a volatile pointer can't be dropped as dead stores */
	volatile unsigned char* p = (volatile unsigned char*)pMem;
	while(uLen--) *p++ = 0;
#endif
}

/* ************************************************************************* */
/* Storage
 *
 * Records live in one DasAry of fixed size cred_rec.  Secrets live in a rank
 * 2 string array, one row each, as "[XXXXXX]secret": six hex digits of flags
 * (24 bits, read as an ordinary hex number, assigned from bit 0 up) and then
 * the secret.  Rows can't be resized in place, so a replaced or removed
 * secret is tombstoned: its prefix becomes "[------]" and its text is wiped.
 * When dead rows outweigh live ones the array is rebuilt.
 *
 * Flag bits: the low 24 are stored with the secret (DASCRED_REJECTED); the
 * high 8 are in memory only, either worked out when copied (DASCRED_EXPIRED)
 * or bookkeeping on the record (the _REC_ bits below).
 */

#define SEC_PREFIX_LEN 8
#define SEC_STORED     0x00FFFFFF

#define REC_REMOVED    0x02000000  /* gone, kept so IDs are never reused */
#define REC_DIRTY      0x04000000  /* changed here since the last load/save */
#define REC_SEEN       0x08000000  /* used only during a load or save */

#define MSG_SZ  1024

#define STORE_NONE 0
#define STORE_FILE 1

typedef struct cred_rec {
	int16_t    nCred;
	uint32_t   uFlags;      /* _REC_ bits only, secret flags are in its row */
	das_auth_e eScheme;
	int64_t    nExpires;    /* Unix seconds UTC, 0 for never */
	ptrdiff_t  iSecret;     /* row in pSecrets, -1 for none */
	char sServer[DASCRED_SRV_SZ];
	char sRealm[DASCRED_REALM_SZ];
	char sDataset[DASCRED_DSET_SZ];
} cred_rec;

struct das_credmngr {
	DasAry*    pRecs;
	DasAry*    pSecrets;
	size_t     uLiveBytes;   /* in rows still referenced */
	size_t     uDeadBytes;   /* in tombstoned rows */
	int        nNextId;
	das_prompt fPrompt;
	void*      pPromptUser;
	int        nStore;
	char       sPath[DASCRED_STORE_SZ];
	char       sStoreDesc[DASCRED_STORE_SZ + 8];
	char       sLastMsg[MSG_SZ];
};

static cred_rec* _DasCredMngr_recs(const DasCredMngr* pThis, size_t* pN)
{
	size_t uN = 0;
	ubyte* p = NULL;
	if(DasAry_size(pThis->pRecs) > 0)
		p = DasAry_getBuf(pThis->pRecs, vtUnknown, DIM0, &uN);
	*pN = (p == NULL) ? 0 : uN;
	return (cred_rec*)p;
}

static bool _recLive(const cred_rec* pRec){ return !(pRec->uFlags & REC_REMOVED); }

static cred_rec* _DasCredMngr_recById(const DasCredMngr* pThis, int16_t nCred)
{
	if(nCred < 1) return NULL;
	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	for(size_t u = 0; u < uN; ++u)
		if((pRecs[u].nCred == nCred) && _recLive(pRecs + u)) return pRecs + u;
	return NULL;
}

/* Exact match on all three strings, live records only, or only removed ones
   with changes not yet saved */
static cred_rec* _DasCredMngr_findExact(
	const DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, bool bRemoved
){
	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	for(size_t u = 0; u < uN; ++u){
		cred_rec* p = pRecs + u;
		if(bRemoved){
			if(_recLive(p) || !(p->uFlags & REC_DIRTY)) continue;
		}
		else if(!_recLive(p)) continue;

		if((strcmp(p->sServer, sServer) == 0) && (strcmp(p->sRealm, sRealm) == 0) &&
		   (strcmp(p->sDataset, sDataset) == 0))
			return p;
	}
	return NULL;
}

static char* _DasCredMngr_row(const DasCredMngr* pThis, ptrdiff_t iRow)
{
	size_t uLen = 0;
	return (char*)DasAry_getBuf(pThis->pSecrets, vtUByte, DIM1_AT(iRow), &uLen);
}

static bool _prefixRead(const char* s, uint32_t* pFlags)
{
	if(s[0] != '[') return false;
	uint32_t uFlags = 0;
	for(int i = 1; i < 7; ++i){  /* stops at a terminator, it isn't hex */
		char c = s[i];
		uint32_t n;
		if((c >= '0')&&(c <= '9'))      n = c - '0';
		else if((c >= 'A')&&(c <= 'F')) n = c - 'A' + 10;
		else if((c >= 'a')&&(c <= 'f')) n = c - 'a' + 10;
		else return false;
		uFlags = (uFlags << 4) | n;
	}
	if(s[7] != ']') return false;
	*pFlags = uFlags;
	return true;
}

static void _prefixWrite(char* s, uint32_t uFlags)
{
	static const char aHex[] = "0123456789ABCDEF";
	s[0] = '[';
	for(int i = 0; i < 6; ++i) s[1+i] = aHex[(uFlags >> (20 - 4*i)) & 0xF];
	s[7] = ']';
}

static uint32_t _DasCredMngr_secFlags(const DasCredMngr* pThis, const cred_rec* pRec)
{
	uint32_t uFlags = 0;
	if(pRec->iSecret >= 0) _prefixRead(_DasCredMngr_row(pThis, pRec->iSecret), &uFlags);
	return uFlags & SEC_STORED;
}

static const char* _DasCredMngr_secText(const DasCredMngr* pThis, const cred_rec* pRec)
{
	if(pRec->iSecret < 0) return "";
	return _DasCredMngr_row(pThis, pRec->iSecret) + SEC_PREFIX_LEN;
}

static bool _recExpired(const cred_rec* pRec)
{
	return (pRec->nExpires != 0) && (pRec->nExpires <= (int64_t)time(NULL));
}

static bool _DasCredMngr_usable(const DasCredMngr* pThis, const cred_rec* pRec)
{
	return (pRec->iSecret >= 0) && (pRec->eScheme != DASAUTH_NONE) &&
	       !(_DasCredMngr_secFlags(pThis, pRec) & DASCRED_REJECTED) &&
	       !_recExpired(pRec);
}

static DasAry* _newSecrets(void)
{
	DasAry* pAry = new_DasAry(
		"cred_secrets", vtUByte, 0, NULL, RANK_2(0,0), UNIT_DIMENSIONLESS
	);
	/* The array zeroes each block it lets go of, as it grows and when freed */
	if(pAry) DasAry_setUsage(pAry, D2ARY_AS_STRING | D2ARY_WIPE);
	return pAry;
}

/* Rebuild the secrets array without its tombstones */
static void _DasCredMngr_compact(DasCredMngr* pThis)
{
	DasAry* pNew = _newSecrets();
	if(pNew == NULL) return;   /* keep going with the old one */

	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	ptrdiff_t iNext = 0;
	for(size_t u = 0; u < uN; ++u){
		if(pRecs[u].iSecret < 0) continue;
		const char* s = _DasCredMngr_row(pThis, pRecs[u].iSecret);
		DasAry_append(pNew, (const ubyte*)s, strlen(s) + 1);
		DasAry_markEnd(pNew, DIM1);
		pRecs[u].iSecret = iNext++;
	}
	dec_DasAry(pThis->pSecrets);
	pThis->pSecrets = pNew;
	pThis->uDeadBytes = 0;
}

/* Tombstone a record's secret, if it has one */
static void _DasCredMngr_killSecret(DasCredMngr* pThis, cred_rec* pRec)
{
	if(pRec->iSecret < 0) return;
	char* s = _DasCredMngr_row(pThis, pRec->iSecret);
	size_t uLen = strlen(s);
	memcpy(s, "[------]", SEC_PREFIX_LEN);
	_dascred_wipe(s + SEC_PREFIX_LEN, uLen - SEC_PREFIX_LEN);
	pRec->iSecret = -1;
	pThis->uLiveBytes -= uLen + 1;
	pThis->uDeadBytes += uLen + 1;

	if((pThis->uDeadBytes > 4096) && (pThis->uDeadBytes > pThis->uLiveBytes))
		_DasCredMngr_compact(pThis);
}

/* Give a record a new secret row; any old one must be killed first */
static bool _DasCredMngr_putSecret(
	DasCredMngr* pThis, cred_rec* pRec, uint32_t uFlags, const char* sSecret
){
	char sPre[SEC_PREFIX_LEN];
	_prefixWrite(sPre, uFlags & SEC_STORED);
	size_t uLen = strlen(sSecret);
	if((DasAry_append(pThis->pSecrets, (const ubyte*)sPre, SEC_PREFIX_LEN) == NULL) ||
	   (DasAry_append(pThis->pSecrets, (const ubyte*)sSecret, uLen + 1) == NULL))
		return false;
	DasAry_markEnd(pThis->pSecrets, DIM1);
	pRec->iSecret = (ptrdiff_t)DasAry_lengthIn(pThis->pSecrets, DIM0) - 1;
	pThis->uLiveBytes += SEC_PREFIX_LEN + uLen + 1;
	return true;
}

/* Server, realm and dataset are saved in a '|' separated file and some go
   out in HTTP headers, so no separators and no control characters.  UTF-8
   is fine. */
static bool _okText(const char* s, size_t uMax, const char* sWhat, bool bLog)
{
	size_t uLen = strlen(s);
	if(uLen >= uMax){
		if(bLog)
			das_error(DASERR_CRED, "The %s is %zu bytes, the limit is %zu", sWhat,
			          uLen, uMax - 1);
		return false;
	}
	for(const unsigned char* p = (const unsigned char*)s; *p != '\0'; ++p){
		if((*p < 0x20)||(*p == 0x7F)||(*p == '|')){
			if(bLog)
				das_error(DASERR_CRED, "The %s contains a '|' or a control character",
				          sWhat);
			return false;
		}
	}
	return true;
}

/* Secrets go into an Authorization header verbatim.  Base64 and RFC 6750
   b64token are both printable ASCII without spaces, anything else could
   split the header. */
static bool _okSecret(const char* s, bool bLog)
{
	size_t uLen = strlen(s);
	if((uLen == 0) || (uLen >= DASCRED_SECRET_SZ)){
		if(bLog)
			das_error(DASERR_CRED, "A secret must be 1 to %d bytes long",
			          DASCRED_SECRET_SZ - 1);
		return false;
	}
	for(const unsigned char* p = (const unsigned char*)s; *p != '\0'; ++p){
		if((*p < 0x21)||(*p > 0x7E)||(*p == '|')){
			if(bLog)
				das_error(DASERR_CRED, "A secret may only hold printable ASCII "
				          "other than '|' and space");
			return false;
		}
	}
	return true;
}

/* Returns the new ID, or -1 if the manager has used them all */
static int16_t _DasCredMngr_newRec(
	DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, uint32_t uFlags
){
	if(pThis->nNextId > INT16_MAX){
		das_error(DASERR_CRED, "This credentials manager has made %d credentials, "
		          "the most one can", INT16_MAX);
		return -1;
	}
	cred_rec rec;
	memset(&rec, 0, sizeof(cred_rec));
	rec.nCred = (int16_t)pThis->nNextId;
	rec.uFlags = uFlags;
	rec.eScheme = DASAUTH_NONE;
	rec.iSecret = -1;
	/* Lengths were checked, these always fit */
	strncpy(rec.sServer, sServer, DASCRED_SRV_SZ - 1);
	strncpy(rec.sRealm, sRealm, DASCRED_REALM_SZ - 1);
	strncpy(rec.sDataset, sDataset, DASCRED_DSET_SZ - 1);

	if(DasAry_append(pThis->pRecs, (const ubyte*)&rec, 1) == NULL) return -1;
	++(pThis->nNextId);
	return rec.nCred;
}

/* ************************************************************************* */
/* Credential records */

void das_cred_init(
	das_cred* pCred, char* sServer, size_t uServerSz, char* sRealm,
	size_t uRealmSz, char* sDataset, size_t uDatasetSz
){
	memset(pCred, 0, sizeof(das_cred));  /* zeroed expires is the null datum */

	pCred->sServer = sServer;   pCred->uServerSz  = sServer  ? uServerSz : 0;
	pCred->sRealm = sRealm;     pCred->uRealmSz   = sRealm   ? uRealmSz : 0;
	pCred->sDataset = sDataset; pCred->uDatasetSz = sDataset ? uDatasetSz : 0;

	if(pCred->uServerSz > 0)  sServer[0] = '\0';
	if(pCred->uRealmSz > 0)   sRealm[0] = '\0';
	if(pCred->uDatasetSz > 0) sDataset[0] = '\0';
}

static void _copyStr(char* sDest, size_t uSz, const char* sSrc, int* pRet)
{
	if((sDest == NULL)||(uSz == 0)) return;
	size_t uLen = strlen(sSrc);
	if(uLen >= uSz){
		uLen = uSz - 1;
		*pRet = DASCRED_SHORT;
	}
	memcpy(sDest, sSrc, uLen);
	sDest[uLen] = '\0';
}

static int _DasCredMngr_copyOut(
	const DasCredMngr* pThis, const cred_rec* pRec, das_cred* pOut
){
	int nRet = DAS_OKAY;
	pOut->nCred = pRec->nCred;
	pOut->uFlags = _DasCredMngr_secFlags(pThis, pRec) |
	               (_recExpired(pRec) ? DASCRED_EXPIRED : 0);
	pOut->eScheme = (pRec->iSecret >= 0) ? pRec->eScheme : DASAUTH_NONE;
	if(pRec->nExpires != 0)
		das_datum_init(
			&(pOut->expires), (const ubyte*)&(pRec->nExpires), vtLong, 0, UNIT_T1970
		);
	else
		das_datum_initNull(&(pOut->expires));

	_copyStr(pOut->sServer, pOut->uServerSz, pRec->sServer, &nRet);
	_copyStr(pOut->sRealm, pOut->uRealmSz, pRec->sRealm, &nRet);
	_copyStr(pOut->sDataset, pOut->uDatasetSz, pRec->sDataset, &nRet);
	return nRet;
}

/* ************************************************************************* */
/* Construction */

DasCredMngr* new_DasCredMngr(void)
{
	DasCredMngr* pThis = (DasCredMngr*)calloc(1, sizeof(DasCredMngr));
	if(pThis == NULL) return NULL;

	cred_rec fill;
	memset(&fill, 0, sizeof(cred_rec));
	pThis->pRecs = new_DasAry(
		"cred_records", vtUnknown, sizeof(cred_rec), (const ubyte*)&fill,
		RANK_1(0), UNIT_DIMENSIONLESS
	);
	pThis->pSecrets = _newSecrets();
	if((pThis->pRecs == NULL)||(pThis->pSecrets == NULL)){
		if(pThis->pRecs) dec_DasAry(pThis->pRecs);
		if(pThis->pSecrets) dec_DasAry(pThis->pSecrets);
		free(pThis);
		return NULL;
	}
	pThis->nNextId = 1;
	pThis->nStore = STORE_NONE;
	return pThis;
}

void del_DasCredMngr(DasCredMngr* pThis)
{
	if(pThis == NULL) return;
	dec_DasAry(pThis->pSecrets);
	dec_DasAry(pThis->pRecs);
	free(pThis);
}

/* ************************************************************************* */
/* Records and secrets */

int16_t DasCredMngr_addCred(
	DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset
){
	if((pThis == NULL)||(sServer == NULL)||(sServer[0] == '\0')){
		das_error(DASERR_CRED, "A credential needs a manager and a server");
		return -1;
	}
	if(sRealm == NULL) sRealm = "";
	if(sDataset == NULL) sDataset = "";

	if(!_okText(sServer, DASCRED_SRV_SZ, "server", true) ||
	   !_okText(sRealm, DASCRED_REALM_SZ, "realm", true) ||
	   !_okText(sDataset, DASCRED_DSET_SZ, "dataset", true))
		return -1;

	cred_rec* pRec = _DasCredMngr_findExact(pThis, sServer, sRealm, sDataset, false);
	if(pRec != NULL) return pRec->nCred;

	return _DasCredMngr_newRec(pThis, sServer, sRealm, sDataset, REC_DIRTY);
}

int DasCredMngr_setSecret(
	DasCredMngr* pThis, int16_t nCred, das_auth_e eScheme, const char* sSecret,
	const das_datum* pExpires
){
	if(pThis == NULL) return das_error(DASERR_CRED, "NULL credentials manager");

	cred_rec* pRec = _DasCredMngr_recById(pThis, nCred);
	if(pRec == NULL) return DASCRED_NONE;

	if(eScheme == DASAUTH_NONE){
		_DasCredMngr_killSecret(pThis, pRec);
		pRec->eScheme = DASAUTH_NONE;
		pRec->nExpires = 0;
		pRec->uFlags |= REC_DIRTY;
		return DAS_OKAY;
	}

	if((eScheme != DASAUTH_BASIC)&&(eScheme != DASAUTH_BEARER))
		return das_error(DASERR_CRED, "Unknown authentication scheme %d", (int)eScheme);
	if(sSecret == NULL)
		return das_error(DASERR_CRED, "NULL secret");
	if(!_okSecret(sSecret, true))
		return DASERR_CRED;

	int64_t nExpires = 0;
	if((pExpires != NULL) && !das_datum_isNull(pExpires)){
		double rSec;
		if(!das_datum_toEpoch(pExpires, UNIT_T1970, &rSec))
			return das_error(DASERR_CRED, "The expiration is not a time");
		nExpires = (int64_t)rSec;
		if(nExpires == 0) nExpires = 1;  /* 0 means never */
	}

	_DasCredMngr_killSecret(pThis, pRec);
	if(!_DasCredMngr_putSecret(pThis, pRec, 0, sSecret))
		return das_error(DASERR_CRED, "Could not store the secret");
	pRec->eScheme = eScheme;
	pRec->nExpires = nExpires;
	pRec->uFlags |= REC_DIRTY;
	return DAS_OKAY;
}

int DasCredMngr_setUserPass(
	DasCredMngr* pThis, int16_t nCred, const char* sUser, const char* sPass
){
	if(pThis == NULL) return das_error(DASERR_CRED, "NULL credentials manager");
	if(_DasCredMngr_recById(pThis, nCred) == NULL) return DASCRED_NONE;
	if((sUser == NULL)||(sPass == NULL))
		return das_error(DASERR_CRED, "NULL user name or password");
	if(strchr(sUser, ':') != NULL)
		return das_error(DASERR_CRED, "The user name cannot contain a colon, ':', character");

	size_t uUser = strlen(sUser);
	size_t uPass = strlen(sPass);
	char sPair[DASCRED_USER_SZ + DASCRED_PASS_SZ];
	if(uUser + uPass + 2 > sizeof(sPair))
		return das_error(DASERR_CRED, "The user name and password are more than "
		                 "%zu bytes together", sizeof(sPair) - 2);

	memcpy(sPair, sUser, uUser);
	sPair[uUser] = ':';
	memcpy(sPair + uUser + 1, sPass, uPass);

	size_t uLen = 0;
	char* sHash = das_b64_encode((const unsigned char*)sPair, uUser + 1 + uPass, &uLen);
	_dascred_wipe(sPair, sizeof(sPair));
	if(sHash == NULL)
		return das_error(DASERR_CRED, "Could not encode the login");

	int nRet = DasCredMngr_setSecret(pThis, nCred, DASAUTH_BASIC, sHash, NULL);
	_dascred_wipe(sHash, uLen);
	free(sHash);
	return nRet;
}

int DasCredMngr_removeCred(DasCredMngr* pThis, int16_t nCred)
{
	if(pThis == NULL) return das_error(DASERR_CRED, "NULL credentials manager");
	cred_rec* pRec = _DasCredMngr_recById(pThis, nCred);
	if(pRec == NULL) return DASCRED_NONE;
	_DasCredMngr_killSecret(pThis, pRec);
	pRec->uFlags |= (REC_REMOVED | REC_DIRTY);
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Lookup */

/* The best record for a request.  A record for the request's dataset beats
 * one for the whole server, a record naming the request's realm beats one for
 * any realm.  A request with no realm takes a record with any realm. */
static cred_rec* _DasCredMngr_match(
	const DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, bool bUsable
){
	if(sRealm == NULL) sRealm = "";
	if(sDataset == NULL) sDataset = "";

	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	cred_rec* pBest = NULL;
	int nBest = -1;
	for(size_t u = 0; u < uN; ++u){
		cred_rec* p = pRecs + u;
		if(!_recLive(p) || (strcmp(p->sServer, sServer) != 0)) continue;

		int nScore = 0;
		if(p->sDataset[0] != '\0'){
			if(strcmp(p->sDataset, sDataset) != 0) continue;
			nScore += 2;
		}
		if((sRealm[0] != '\0')&&(p->sRealm[0] != '\0')){
			if(strcmp(p->sRealm, sRealm) != 0) continue;
			nScore += 1;
		}
		if(bUsable && !_DasCredMngr_usable(pThis, p)) continue;

		if(nScore > nBest){ pBest = p; nBest = nScore; }
	}
	return pBest;
}

int DasCredMngr_getCred(
	const DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, bool bValidOnly, das_cred* pOut
){
	if((pThis == NULL)||(sServer == NULL)||(pOut == NULL))
		return das_error(DASERR_CRED, "NULL manager, server or output record");

	const cred_rec* pRec = _DasCredMngr_match(
		pThis, sServer, sRealm, sDataset, bValidOnly
	);
	if(pRec == NULL) return DASCRED_NONE;
	return _DasCredMngr_copyOut(pThis, pRec, pOut);
}

int DasCredMngr_getSecret(
	const DasCredMngr* pThis, int16_t nCred, char* sBuf, size_t uLen
){
	if(pThis == NULL){
		das_error(DASERR_CRED, "NULL credentials manager");
		return DASCRED_NONE;
	}
	const cred_rec* pRec = _DasCredMngr_recById(pThis, nCred);
	if(pRec == NULL) return DASCRED_NONE;

	const char* sSecret = _DasCredMngr_secText(pThis, pRec);
	size_t uSecret = strlen(sSecret);
	if((sBuf != NULL)&&(uLen > 0)){
		size_t uCopy = (uSecret < uLen) ? uSecret : uLen - 1;
		memcpy(sBuf, sSecret, uCopy);
		sBuf[uCopy] = '\0';
	}
	return (int)uSecret;
}

int DasCredMngr_numCreds(const DasCredMngr* pThis)
{
	if(pThis == NULL) return 0;
	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	int nLive = 0;
	for(size_t u = 0; u < uN; ++u) if(_recLive(pRecs + u)) ++nLive;
	return nLive;
}

int DasCredMngr_getCredByIdx(const DasCredMngr* pThis, int i, das_cred* pOut)
{
	if((pThis == NULL)||(pOut == NULL))
		return das_error(DASERR_CRED, "NULL manager or output record");
	if(i < 0) return DASCRED_NONE;

	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
	int nLive = 0;
	for(size_t u = 0; u < uN; ++u){
		if(!_recLive(pRecs + u)) continue;
		if(nLive == i) return _DasCredMngr_copyOut(pThis, pRecs + u, pOut);
		++nLive;
	}
	return DASCRED_NONE;
}

/* ************************************************************************* */
/* Prompts */

das_prompt DasCredMngr_setPrompt(
	DasCredMngr* pThis, das_prompt fPrompt, void* pUser
){
	das_prompt fOld = pThis->fPrompt;
	pThis->fPrompt = fPrompt;
	pThis->pPromptUser = pUser;
	return fOld;
}

das_prompt DasCredMngr_getPrompt(const DasCredMngr* pThis, void** ppUser)
{
	if(ppUser != NULL) *ppUser = pThis->pPromptUser;
	return pThis->fPrompt;
}

/* ************************************************************************* */
/* The HTTP client's side door, see creds_p.h */

static int16_t _DasCredMngr_authValue(
	const DasCredMngr* pThis, const cred_rec* pRec, char* sBuf, size_t uLen
){
	const char* sScheme = (pRec->eScheme == DASAUTH_BEARER) ? "Bearer" : "Basic";
	int nLen = snprintf(sBuf, uLen, "%s %s", sScheme, _DasCredMngr_secText(pThis, pRec));
	if((nLen < 0)||((size_t)nLen >= uLen)){
		_dascred_wipe(sBuf, uLen);
		return DASCRED_SHORT;
	}
	return pRec->nCred;
}

int16_t _DasCredMngr_getHttpAuth(
	DasCredMngr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, char* sBuf, size_t uLen
){
	if((pThis == NULL)||(sServer == NULL)||(sBuf == NULL)||(uLen == 0))
		return DASCRED_NONE;

	const cred_rec* pRec = _DasCredMngr_match(pThis, sServer, sRealm, sDataset, true);
	if(pRec != NULL) return _DasCredMngr_authValue(pThis, pRec, sBuf, uLen);

	if(pThis->fPrompt == NULL) return DASCRED_NONE;

	char sUser[DASCRED_USER_SZ];
	char sPass[DASCRED_PASS_SZ];
	const char* sMsg = (pThis->sLastMsg[0] != '\0') ? pThis->sLastMsg : NULL;
	int16_t nRet = DASCRED_NONE;

	while(true){
		sUser[0] = '\0';
		sPass[0] = '\0';
		if(!pThis->fPrompt(pThis->pPromptUser, sServer, sRealm, sDataset, sMsg,
		                   sUser, sizeof(sUser), sPass, sizeof(sPass)))
			break;
		sUser[sizeof(sUser) - 1] = '\0';  /* in case a prompt filled them */
		sPass[sizeof(sPass) - 1] = '\0';

		if(strchr(sUser, ':') != NULL){
			sMsg = "The user name cannot contain a colon, ':', character";
			continue;
		}

		int16_t nCred = DasCredMngr_addCred(pThis, sServer, sRealm, sDataset);
		if(nCred < 1) break;
		if(DasCredMngr_setUserPass(pThis, nCred, sUser, sPass) != DAS_OKAY) break;

		nRet = _DasCredMngr_authValue(
			pThis, _DasCredMngr_recById(pThis, nCred), sBuf, uLen
		);
		pThis->sLastMsg[0] = '\0';
		break;
	}
	_dascred_wipe(sUser, sizeof(sUser));
	_dascred_wipe(sPass, sizeof(sPass));
	return nRet;
}

void _DasCredMngr_authFailed(DasCredMngr* pThis, int16_t nCred, const char* sMsg)
{
	if(pThis == NULL) return;
	cred_rec* pRec = _DasCredMngr_recById(pThis, nCred);
	if((pRec != NULL) && (pRec->iSecret >= 0)){
		char* s = _DasCredMngr_row(pThis, pRec->iSecret);
		uint32_t uFlags = 0;
		_prefixRead(s, &uFlags);
		_prefixWrite(s, uFlags | DASCRED_REJECTED);
		pRec->uFlags |= REC_DIRTY;  /* saved, so other processes don't resend it */
	}
	if(sMsg != NULL)
		snprintf(pThis->sLastMsg, MSG_SZ, "%s", sMsg);
}

/* ************************************************************************* */
/* Stores */

DasErrCode DasCredMngr_setFileStore(DasCredMngr* pThis, const char* sPath)
{
	if(pThis == NULL) return das_error(DASERR_CRED, "NULL credentials manager");

	if((sPath == NULL)||(sPath[0] == '\0')){
		pThis->nStore = STORE_NONE;
		pThis->sPath[0] = '\0';
		pThis->sStoreDesc[0] = '\0';
		return DAS_OKAY;
	}
	if(strlen(sPath) >= DASCRED_STORE_SZ)
		return das_error(DASERR_CRED, "The key file path is %zu bytes, the limit "
		                 "is %d", strlen(sPath), DASCRED_STORE_SZ - 1);

	/* Everything held is new to a different store, so the next save writes it
	   there.  Pointing again at the same file changes nothing. */
	if((pThis->nStore != STORE_FILE)||(strcmp(pThis->sPath, sPath) != 0)){
		size_t uN;
		cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);
		for(size_t u = 0; u < uN; ++u)
			if(_recLive(pRecs + u)) pRecs[u].uFlags |= REC_DIRTY;
	}

	pThis->nStore = STORE_FILE;
	snprintf(pThis->sPath, DASCRED_STORE_SZ, "%s", sPath);
	snprintf(pThis->sStoreDesc, sizeof(pThis->sStoreDesc), "file:%s", sPath);
	return DAS_OKAY;
}

const char* DasCredMngr_store(const DasCredMngr* pThis)
{
	return pThis->sStoreDesc;
}

/* Key file format
 *
 * One credential per line, '|' separated:
 *
 *   server|realm|key|value|key|value|...|[XXXXXX]secret
 *
 * Keys are "scheme" (basic, bearer, none), required, and "dataset" and
 * "expires" (ISO 8601 UTC).  A secret pasted in without the [XXXXXX] prefix
 * has flags 0 and gets one on the next save.
 *
 * Anything else, comments, lines from a newer version with keys this one
 * doesn't know, is kept as is when saving.  Lines with unknown keys are not
 * used: a key this version can't read might narrow where the credential
 * applies, and ignoring it would send the secret too widely.
 */

#define LINE_SZ (DASCRED_SRV_SZ + DASCRED_REALM_SZ + DASCRED_DSET_SZ + \
                  DASCRED_SECRET_SZ + 256)
#define MAX_FIELDS 15

typedef struct cred_line {
	const char* sServer;
	const char* sRealm;
	const char* sDataset;
	das_auth_e  eScheme;
	int64_t     nExpires;
	uint32_t    uFlags;
	const char* sSecret;
} cred_line;

static char* _trim(char* s)
{
	while((*s == ' ')||(*s == '\t')) ++s;
	size_t uLen = strlen(s);
	while((uLen > 0)&&((s[uLen-1] == ' ')||(s[uLen-1] == '\t')||
	      (s[uLen-1] == '\r')||(s[uLen-1] == '\n')))
		s[--uLen] = '\0';
	return s;
}

/* Parse one line in place.  Returns true for a usable credential line.  Lines
   that look like credentials but aren't get a warning, without their text. */
static bool _parseLine(char* sLine, cred_line* pOut, const char* sFile, int nLine)
{
	char* s = _trim(sLine);
	if((s[0] == '\0')||(s[0] == '#')) return false;
	if(strchr(s, '|') == NULL) return false;

	char* aField[MAX_FIELDS + 1];
	int nFields = 0;
	aField[nFields++] = s;
	for(char* p = s; *p != '\0'; ++p){
		if(*p != '|') continue;
		if(nFields > MAX_FIELDS){
			daslog_warn_v("%s,%d: Too many fields, line kept but not used", sFile, nLine);
			return false;
		}
		*p = '\0';
		aField[nFields++] = p + 1;
	}
	for(int i = 0; i < nFields; ++i) aField[i] = _trim(aField[i]);

	if((nFields < 3)||((nFields % 2) == 0)||(aField[0][0] == '\0')){
		daslog_warn_v("%s,%d: Not a credential, line kept but not used", sFile, nLine);
		return false;
	}

	memset(pOut, 0, sizeof(cred_line));
	pOut->sServer = aField[0];
	pOut->sRealm = aField[1];
	pOut->sDataset = "";
	bool bScheme = false;

	for(int i = 2; i < nFields - 1; i += 2){
		const char* sKey = aField[i];
		const char* sVal = aField[i+1];

		if(strcmp(sKey, "dataset") == 0){
			pOut->sDataset = sVal;
		}
		else if(strcmp(sKey, "scheme") == 0){
			bScheme = true;
			if(strcasecmp(sVal, "basic") == 0)       pOut->eScheme = DASAUTH_BASIC;
			else if(strcasecmp(sVal, "bearer") == 0) pOut->eScheme = DASAUTH_BEARER;
			else if(strcasecmp(sVal, "none") == 0)   pOut->eScheme = DASAUTH_NONE;
			else{
				daslog_warn_v("%s,%d: Unknown scheme, line kept but not used", sFile, nLine);
				return false;
			}
		}
		else if(strcmp(sKey, "expires") == 0){
			das_time dt;
			if(!dt_parsetime(&dt, sVal)){
				daslog_warn_v("%s,%d: Unreadable expiration, line kept but not used",
				              sFile, nLine);
				return false;
			}
			pOut->nExpires = (int64_t)Units_convertFromDt(UNIT_T1970, &dt);
			if(pOut->nExpires == 0) pOut->nExpires = 1;
		}
		else{
			daslog_warn_v("%s,%d: Unknown key, line kept but not used", sFile, nLine);
			return false;
		}
	}

	const char* sSecret = aField[nFields - 1];
	if(_prefixRead(sSecret, &(pOut->uFlags)))
		sSecret += SEC_PREFIX_LEN;
	else
		pOut->uFlags = 0;
	pOut->uFlags &= SEC_STORED;

	if(!bScheme){
		daslog_warn_v("%s,%d: No scheme, line kept but not used", sFile, nLine);
		return false;
	}
	pOut->sSecret = (pOut->eScheme == DASAUTH_NONE) ? "" : sSecret;

	if(!_okText(pOut->sServer, DASCRED_SRV_SZ, "server", false) ||
	   !_okText(pOut->sRealm, DASCRED_REALM_SZ, "realm", false) ||
	   !_okText(pOut->sDataset, DASCRED_DSET_SZ, "dataset", false) ||
	   ((pOut->eScheme != DASAUTH_NONE) && !_okSecret(pOut->sSecret, false))){
		daslog_warn_v("%s,%d: A field is too long or holds characters it can't, "
		              "line kept but not used", sFile, nLine);
		return false;
	}
	return true;
}

/* Take a line from the store into memory, the store's version wins.  Returns
   the record, or NULL if it could not be made. */
static cred_rec* _DasCredMngr_absorb(
	DasCredMngr* pThis, cred_rec* pRec, const cred_line* pLine, int* pChanged
){
	if(pRec == NULL){
		int16_t nCred = _DasCredMngr_newRec(
			pThis, pLine->sServer, pLine->sRealm, pLine->sDataset, 0
		);
		if(nCred < 1) return NULL;
		pRec = _DasCredMngr_recById(pThis, nCred);
		++(*pChanged);
	}
	else if((pRec->eScheme != pLine->eScheme) || (pRec->nExpires != pLine->nExpires) ||
	        (_DasCredMngr_secFlags(pThis, pRec) != pLine->uFlags) ||
	        (strcmp(_DasCredMngr_secText(pThis, pRec), pLine->sSecret) != 0)){
		++(*pChanged);
	}
	else{
		pRec->uFlags &= ~REC_DIRTY;
		return pRec;
	}

	_DasCredMngr_killSecret(pThis, pRec);
	if(pLine->eScheme != DASAUTH_NONE)
		_DasCredMngr_putSecret(pThis, pRec, pLine->uFlags, pLine->sSecret);
	pRec->eScheme = pLine->eScheme;
	pRec->nExpires = pLine->nExpires;
	pRec->uFlags &= ~REC_DIRTY;
	return pRec;
}

static bool _DasCredMngr_emit(
	const DasCredMngr* pThis, FILE* pOut, const cred_rec* pRec
){
	const char* sScheme = "none";
	if(pRec->iSecret >= 0){
		if(pRec->eScheme == DASAUTH_BASIC) sScheme = "basic";
		else if(pRec->eScheme == DASAUTH_BEARER) sScheme = "bearer";
	}

	fprintf(pOut, "%s|%s", pRec->sServer, pRec->sRealm);
	if(pRec->sDataset[0] != '\0') fprintf(pOut, "|dataset|%s", pRec->sDataset);
	fprintf(pOut, "|scheme|%s", sScheme);
	if(pRec->nExpires != 0){
		das_time dt;
		char sTime[64];
		Units_convertToDt(&dt, (double)pRec->nExpires, UNIT_T1970);
		fprintf(pOut, "|expires|%s", dt_isoc(&dt, sTime, sizeof(sTime), 0));
	}
	if(pRec->iSecret >= 0)
		fprintf(pOut, "|%s\n", _DasCredMngr_row(pThis, pRec->iSecret));
	else
		fprintf(pOut, "|[000000]\n");

	return !ferror(pOut);
}

/* Locking, a sidecar file so the lock survives the key file being replaced
   by rename */
#ifdef _WIN32
typedef HANDLE cred_lock;
#define NO_LOCK INVALID_HANDLE_VALUE
#else
typedef int cred_lock;
#define NO_LOCK -1
#endif

static cred_lock _lockStore(const char* sPath, bool bWrite)
{
	char sLock[DASCRED_STORE_SZ + 8];
	snprintf(sLock, sizeof(sLock), "%s.lock", sPath);
#ifdef _WIN32
	HANDLE h = CreateFileA(sLock, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL
	);
	if(h == INVALID_HANDLE_VALUE){
		daslog_error_v("Couldn't open lock file '%s'", sLock);
		return NO_LOCK;
	}
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	if(!LockFileEx(h, bWrite ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 1, 0, &ov)){
		daslog_error_v("Couldn't lock '%s'", sLock);
		CloseHandle(h);
		return NO_LOCK;
	}
	return h;
#else
	int nFd = open(sLock, O_RDWR | O_CREAT, 0600);
	if(nFd < 0){
		daslog_error_v("Couldn't open lock file '%s', %s", sLock, strerror(errno));
		return NO_LOCK;
	}
	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = bWrite ? F_WRLCK : F_RDLCK;
	fl.l_whence = SEEK_SET;
	while(fcntl(nFd, F_SETLKW, &fl) == -1){
		if(errno == EINTR) continue;
		daslog_error_v("Couldn't lock '%s', %s", sLock, strerror(errno));
		close(nFd);
		return NO_LOCK;
	}
	return nFd;
#endif
}

static void _unlockStore(cred_lock h)
{
#ifdef _WIN32
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	UnlockFileEx(h, 0, 1, 0, &ov);
	CloseHandle(h);
#else
	close(h);  /* drops the fcntl lock */
#endif
}

/* Refuse key files others can read or change, as ssh does for keys.  On
   Windows files under the user's profile inherit an owner only ACL and
   there is no mode to check. */
static bool _permsOk(const char* sPath)
{
#ifdef _WIN32
	(void)sPath;
	return true;
#else
	struct stat st;
	if(stat(sPath, &st) != 0) return true;   /* missing is handled by the caller */
	if(st.st_uid != geteuid()){
		daslog_error_v("Not using credentials file '%s', it belongs to another "
		               "user", sPath);
		return false;
	}
	if(st.st_mode & 077){
		daslog_error_v("Not using credentials file '%s', other users can read or "
		               "change it.  Fix with: chmod 600 %s", sPath, sPath);
		return false;
	}
	return true;
#endif
}

static FILE* _openTemp(const char* sPath, char* sTmp, size_t uTmp)
{
#ifdef _WIN32
	snprintf(sTmp, uTmp, "%s.tmp", sPath);
	return fopen(sTmp, "wb");
#else
	snprintf(sTmp, uTmp, "%s.XXXXXX", sPath);
	int nFd = mkstemp(sTmp);   /* made 0600 */
	if(nFd < 0) return NULL;
	fchmod(nFd, 0600);
	FILE* pOut = fdopen(nFd, "wb");
	if(pOut == NULL){ close(nFd); remove(sTmp); }
	return pOut;
#endif
}

/* Flush to disk and move into place */
static bool _commitTemp(FILE* pOut, const char* sTmp, const char* sPath)
{
	bool bOkay = (fflush(pOut) == 0);
#ifdef _WIN32
	if(bOkay) bOkay = (_commit(_fileno(pOut)) == 0);
	if(fclose(pOut) != 0) bOkay = false;
	if(bOkay)
		bOkay = MoveFileExA(sTmp, sPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
	if(bOkay) bOkay = (fsync(fileno(pOut)) == 0);
	if(fclose(pOut) != 0) bOkay = false;
	if(bOkay) bOkay = (rename(sTmp, sPath) == 0);
#endif
	if(!bOkay) remove(sTmp);
	return bOkay;
}

/* Load or save.  Both read the store under its lock and take the store's
 * version of each credential, except that a save keeps this manager's
 * unsaved changes and removals and writes the result back.  Returns the
 * count of credentials changed in memory (load) or written (save), or -1. */
static int _DasCredMngr_sync(DasCredMngr* pThis, bool bSave)
{
	if(pThis->nStore == STORE_NONE){
		das_error(DASERR_CRED, "No credentials store set, see DasCredMngr_setFileStore()");
		return -1;
	}
	const char* sPath = pThis->sPath;

	if(!bSave && !das_isfile(sPath)) return 0;

	cred_lock hLock = _lockStore(sPath, bSave);
	if(hLock == NO_LOCK) return -1;

	int nRet = -1;
	FILE* pIn = NULL;
	FILE* pOut = NULL;
	char sTmp[DASCRED_STORE_SZ + 16] = {'\0'};
	char* aLine = (char*)malloc(2 * LINE_SZ);  /* line and working copy */
	if(aLine == NULL){
		das_error(DASERR_CRED, "Out of memory");
		goto DONE;
	}
	char* aWork = aLine + LINE_SZ;

	if(das_isfile(sPath)){
		if(!_permsOk(sPath)) goto DONE;
		if((pIn = fopen(sPath, "rb")) == NULL){
			daslog_error_v("Couldn't open '%s' for reading", sPath);
			goto DONE;
		}
	}
	else if(!bSave){
		nRet = 0;   /* removed while we waited for the lock */
		goto DONE;
	}

	if(bSave && ((pOut = _openTemp(sPath, sTmp, sizeof(sTmp))) == NULL)){
		daslog_error_v("Couldn't write a new credentials file beside '%s'", sPath);
		goto DONE;
	}

	int nChanged = 0;
	int nWritten = 0;
	int nLine = 0;
	cred_line line;
	bool bOkay = true;

	while((pIn != NULL) && fgets(aLine, LINE_SZ, pIn)){
		++nLine;
		size_t uLen = strlen(aLine);

		/* Longer than any credential can be, keep it but don't parse it */
		if((uLen == LINE_SZ - 1) && (aLine[uLen - 1] != '\n')){
			daslog_warn_v("%s,%d: Line too long, kept but not used", sPath, nLine);
			if(pOut) fputs(aLine, pOut);
			int c;
			while(((c = fgetc(pIn)) != EOF) && (c != '\n'))
				if(pOut) fputc(c, pOut);
			if(pOut) fputc('\n', pOut);
			continue;
		}

		memcpy(aWork, aLine, uLen + 1);
		if(!_parseLine(aWork, &line, sPath, nLine)){
			if(pOut){
				fputs(aLine, pOut);
				if((uLen == 0)||(aLine[uLen - 1] != '\n')) fputc('\n', pOut);
			}
			continue;
		}

		cred_rec* pRec = _DasCredMngr_findExact(
			pThis, line.sServer, line.sRealm, line.sDataset, false
		);
		if((pRec != NULL) && (pRec->uFlags & REC_SEEN))
			continue;   /* a repeat of an earlier line, the first one wins */

		if(bSave){
			if((pRec != NULL) && (pRec->uFlags & REC_DIRTY)){
				bOkay = bOkay && _DasCredMngr_emit(pThis, pOut, pRec);
				pRec->uFlags |= REC_SEEN;
				++nWritten;
				continue;
			}
			if((pRec == NULL) && (_DasCredMngr_findExact(
				pThis, line.sServer, line.sRealm, line.sDataset, true) != NULL))
				continue;   /* removed here */
		}

		if((pRec = _DasCredMngr_absorb(pThis, pRec, &line, &nChanged)) == NULL){
			if(pOut) fputs(aLine, pOut);   /* manager is full, don't lose it */
			continue;
		}
		pRec->uFlags |= REC_SEEN;
		if(bSave){
			bOkay = bOkay && _DasCredMngr_emit(pThis, pOut, pRec);
			++nWritten;
		}
	}
	if(pIn && ferror(pIn)){
		daslog_error_v("Error reading '%s'", sPath);
		bOkay = false;
	}

	size_t uN;
	cred_rec* pRecs = _DasCredMngr_recs(pThis, &uN);

	if(bSave){
		for(size_t u = 0; u < uN; ++u){
			cred_rec* p = pRecs + u;
			if(_recLive(p) && (p->uFlags & REC_DIRTY) && !(p->uFlags & REC_SEEN)){
				bOkay = bOkay && _DasCredMngr_emit(pThis, pOut, p);
				++nWritten;
			}
		}
		bOkay = _commitTemp(pOut, sTmp, sPath) && bOkay;
		pOut = NULL;
		if(!bOkay) daslog_error_v("Couldn't save credentials to '%s'", sPath);
	}

	for(size_t u = 0; u < uN; ++u){
		cred_rec* p = pRecs + u;

		/* Came from this store, unchanged here, and the store no longer has
		   it: another process removed it, so it goes here too */
		if(bOkay && _recLive(p) && !(p->uFlags & (REC_DIRTY | REC_SEEN))){
			_DasCredMngr_killSecret(pThis, p);
			p->uFlags |= REC_REMOVED;
		}
		p->uFlags &= ~REC_SEEN;
		if(bSave && bOkay) p->uFlags &= ~REC_DIRTY;
	}
	if(bOkay) nRet = bSave ? nWritten : nChanged;

DONE:
	if(pIn) fclose(pIn);
	if(pOut){ fclose(pOut); remove(sTmp); }
	if(aLine){
		_dascred_wipe(aLine, 2 * LINE_SZ);
		free(aLine);
	}
	_unlockStore(hLock);
	return nRet;
}

int DasCredMngr_load(DasCredMngr* pThis)
{
	if(pThis == NULL){ das_error(DASERR_CRED, "NULL credentials manager"); return -1; }
	return _DasCredMngr_sync(pThis, false);
}

int DasCredMngr_save(DasCredMngr* pThis)
{
	if(pThis == NULL){ das_error(DASERR_CRED, "NULL credentials manager"); return -1; }
	return _DasCredMngr_sync(pThis, true);
}
