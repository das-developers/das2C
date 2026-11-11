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

/* The credentials manager's side door for the HTTP client.  These are where
 * token refresh, rotation and sender constrained schemes (DPoP) will land, so
 * they stay private and free to change.  See the OAuth notes in creds.c. */

#ifndef _das_creds_p_h_
#define _das_creds_p_h_

#include "creds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Room for a whole Authorization header value: scheme name, space, secret */
#define DASCRED_AUTH_SZ (DASCRED_SECRET_SZ + 16)

/* Get the Authorization header value for a request, prompting if needed
 *
 * A usable credential (has a secret, not rejected, not expired) is used if
 * one matches.  Otherwise the prompt, if any, is asked and what it returns
 * is kept for next time.
 *
 * sBuf gets the whole value, "Basic ..." or "Bearer ...", for uLen at
 * least DASCRED_AUTH_SZ.
 *
 * Returns the credential's ID, or DASCRED_NONE if none could be had (no
 * match and no prompt, or the user declined).  The caller wipes sBuf when
 * done with it.
 */
int16_t _DasCredMngr_getHttpAuth(
	DasCredMgr* pThis, const char* sServer, const char* sRealm,
	const char* sDataset, char* sBuf, size_t uLen
);

/* Mark the secret the server just refused (HTTP 401)
 *
 * It is not sent again on its own.  sMsg, if not NULL, is shown at the next
 * prompt.  Only a 401 means the secret was wrong; 403, 5xx and timeouts say
 * nothing about it and must not come here.
 */
void _DasCredMngr_authFailed(DasCredMgr* pThis, int16_t nCred, const char* sMsg);

/* Wipe memory that held a secret, in a way the compiler can't drop */
void _dascred_wipe(void* pMem, size_t uLen);

#ifdef __cplusplus
}
#endif

#endif /* _das_creds_p_h_ */
