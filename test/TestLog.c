/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* The error and log facility: daserrf() messages arrive in the log hook
   with the caller's location, the disposition decides what happens after,
   and the hook contract holds (re-entry doesn't hang, atexit() runs after
   the lock is released). */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define HAVE_FORK
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <das3/core.h>

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)
#define CHECK(C, ...) do{ if(!(C)) FAIL(__VA_ARGS__); }while(0)

/* ************************************************************************* */
/* A hook that keeps a copy of the last message */

typedef struct capture {
	int  nCalls;
	int  nLevel;
	int  nErr;
	int  nLine;
	char sMsg[DASLOG_MSG_SZ];
	char sFunc[64];
} capture_t;

static void _capture(const DasLogMsg* pMsg, void* pUser)
{
	capture_t* pCap = (capture_t*)pUser;
	pCap->nCalls += 1;
	pCap->nLevel = pMsg->level;
	pCap->nErr   = pMsg->errCode;
	pCap->nLine  = pMsg->line;
	snprintf(pCap->sMsg, sizeof(pCap->sMsg), "%s", pMsg->msg);
	snprintf(pCap->sFunc, sizeof(pCap->sFunc), "%s",
	         (pMsg->func != NULL) ? pMsg->func : "");
}

static capture_t g_cap;

static void _reset(void){ memset(&g_cap, 0, sizeof(g_cap)); }

/* ************************************************************************* */

static int _raiseUnits(void)
{
	return daserrf(DASERR_UNITS, "bad units %d", 42);
}

static void testErrors(void)
{
	_reset();
	int nLine = __LINE__ + 1;
	int nRet = daserrf(DASERR_ARRAY, "array %s", "trouble");
	CHECK(nRet == DASERR_ARRAY, "daserrf returned %d in return mode", nRet);
	CHECK(g_cap.nCalls == 1, "hook called %d times", g_cap.nCalls);
	CHECK(g_cap.nLevel == DASLOG_ERROR, "level %d, not ERROR", g_cap.nLevel);
	CHECK(g_cap.nErr == DASERR_ARRAY, "nErr %d", g_cap.nErr);
	CHECK(strcmp(g_cap.sMsg, "array trouble") == 0, "text '%s'", g_cap.sMsg);
	CHECK(g_cap.nLine == nLine, "line %d, expected %d", g_cap.nLine, nLine);
	CHECK(strcmp(g_cap.sFunc, "testErrors") == 0, "function '%s'", g_cap.sFunc);

	/* The location is the caller's, not the library's */
	_reset();
	_raiseUnits();
	CHECK(strcmp(g_cap.sFunc, "_raiseUnits") == 0, "function '%s'", g_cap.sFunc);
	CHECK(strcmp(g_cap.sMsg, "bad units 42") == 0, "text '%s'", g_cap.sMsg);

	_reset();
	CHECK(daserr_falsef(DASERR_UTIL, "x") == false, "daserr_falsef");
	CHECK(daserr_nullf(DASERR_UTIL, "x") == NULL, "daserr_nullf");
	CHECK(g_cap.nCalls == 2, "both logged, %d calls", g_cap.nCalls);

	/* Not a format string, a '%' passes through */
	_reset();
	daserr(DASERR_UTIL, "100% sure, %s %d");
	CHECK(strcmp(g_cap.sMsg, "100% sure, %s %d") == 0, "text '%s'", g_cap.sMsg);
}

static void testLog(void)
{
	_reset();
	daslog_infof("plain %s", "info");
	CHECK(g_cap.nCalls == 1, "info delivered");
	CHECK(g_cap.nErr == DAS_OKAY, "plain log nErr %d", g_cap.nErr);
	CHECK(g_cap.nLevel == DASLOG_INFO, "level %d", g_cap.nLevel);

	/* Plain macros don't treat the message as a format */
	_reset();
	daslog_warn("50% done");
	CHECK(strcmp(g_cap.sMsg, "50% done") == 0, "text '%s'", g_cap.sMsg);

	/* Below the level never reaches the hook */
	int nOld = daslog_setLevel(DASLOG_WARN);
	CHECK(nOld == DASLOG_INFO, "previous level %d", nOld);
	_reset();
	daslog_info("dropped");
	daslog_debug("dropped");
	CHECK(g_cap.nCalls == 0, "%d messages below the level delivered", g_cap.nCalls);

	/* A bad level warns, changes nothing, and hands back a usable level */
	_reset();
	int nRet = daslog_setLevel(999);
	CHECK(nRet == DASLOG_WARN, "bad level returned %d", nRet);
	CHECK(daslog_level() == DASLOG_WARN, "level moved to %d", daslog_level());
	CHECK((g_cap.nCalls == 1)&&(g_cap.nLevel == DASLOG_WARN), "bad level warned");
	daslog_setLevel(nOld);

	/* With return on errors, a level above ERROR hides daserrf() text.  The
	   call is still allowed after daslib_init(), on purpose. */
	daslog_setLevel(DASLOG_CRITICAL);
	_reset();
	CHECK(daserrf(DASERR_UTIL, "hidden") == DASERR_UTIL, "code still returned");
	CHECK(g_cap.nCalls == 0, "hidden error delivered");
	daslog_setLevel(nOld);

	/* Messages too long for the buffer are cut, and say so */
	char* sLong = (char*)malloc(3 * DASLOG_MSG_SZ);
	memset(sLong, 'x', 3 * DASLOG_MSG_SZ - 1);
	sLong[3 * DASLOG_MSG_SZ - 1] = '\0';
	_reset();
	daslog_infof("%s", sLong);
	size_t uLen = strlen(g_cap.sMsg);
	CHECK(uLen == DASLOG_MSG_SZ - 1, "cut message is %zu bytes", uLen);
	CHECK(strcmp(g_cap.sMsg + uLen - 3, "...") == 0, "cut message ends '%s'",
	      g_cap.sMsg + uLen - 3);
	free(sLong);
}

static void testLevels(void)
{
	CHECK(daslog_levelFromStr("Warning") == DASLOG_WARN, "warn");
	CHECK(daslog_levelFromStr("crit") == DASLOG_CRITICAL, "crit");
	CHECK(daslog_levelFromStr("nope") == -1, "unknown is -1");
	CHECK(daslog_levelFromStr(NULL) == -1, "NULL is -1");
	CHECK(strcmp(daslog_levelToStr(70), "WARNING") == 0, "70 rounds down");
	CHECK(strcmp(daslog_levelToStr(DASLOG_CRITICAL), "CRITICAL") == 0, "crit name");
	CHECK(strcmp(daslog_levelToStr(-1), "") == 0, "negative is empty");
}

/* Swapping hooks is a pair operation that restores exactly */
static void testHooks(void)
{
	capture_t other = {0};
	DasLogHook hook = {_capture, &other};
	DasLogHook prev = daslog_setHook(&hook);
	CHECK((prev.callback == _capture)&&(prev.userData == &g_cap), "previous hook returned");

	_reset();
	daslog_warn("to the other one");
	CHECK((other.nCalls == 1)&&(g_cap.nCalls == 0), "pUser passed through");

	DasLogHook mine = daslog_setHook(NULL);
	CHECK(mine.userData == &other, "swapped hook returned");
	DasLogHook dflt = daslog_setHook(&prev);
	CHECK((dflt.callback == daslog_stderr)&&(dflt.userData == NULL), "NULL installs the default");

	_reset();
	daslog_warn("back again");
	CHECK(g_cap.nCalls == 1, "restored hook delivers");
}

/* A handler that calls back into the library breaks the contract.  The
   nested message is dropped with a note on stderr, but nothing hangs. */
static int g_nOuter = 0;
static void _reenter(const DasLogMsg* pMsg, void* pUser)
{
	(void)pMsg; (void)pUser;
	++g_nOuter;
	daslog_info("nested");
}

static void testReentry(void)
{
	DasLogHook hook = {_reenter, NULL};
	DasLogHook prev = daslog_setHook(&hook);

#ifdef HAVE_FORK
	/* Keep the expected note off a passing run's output */
	fflush(stderr);
	int nSave = dup(2);
	FILE* pNull = freopen("/dev/null", "w", stderr);
	(void)pNull;
#endif
	daslog_info("outer");
#ifdef HAVE_FORK
	fflush(stderr);
	dup2(nSave, 2);
	close(nSave);
#endif

	daslog_setHook(&prev);
	CHECK(g_nOuter == 1, "outer message delivered %d times", g_nOuter);
}

/* In exit mode daserrf() logs at CRITICAL, then exits with its code.  The
   lock must be released by then, so an atexit() function can still log. */
#ifdef HAVE_FORK
static int g_fdOut = -1;
static int g_nChildLevel = -1;
static int g_nChildCalls = 0;

static void _childHook(const DasLogMsg* pMsg, void* pUser)
{
	(void)pUser;
	if(pMsg->errCode != DAS_OKAY) g_nChildLevel = pMsg->level;
	++g_nChildCalls;
}

static void _childAtExit(void)
{
	daslog_warn("from atexit");   /* would be dropped if the lock were held */
	int aRes[2] = {g_nChildLevel, g_nChildCalls};
	if(write(g_fdOut, aRes, sizeof(aRes)) != (ssize_t)sizeof(aRes)) _exit(99);
}

static void testExit(void)
{
	int aPipe[2];
	if(pipe(aPipe) != 0){ FAIL("pipe"); return; }

	pid_t pid = fork();
	if(pid < 0){ FAIL("fork"); return; }
	if(pid == 0){
		close(aPipe[0]);
		g_fdOut = aPipe[1];
		DasLogHook hook = {_childHook, NULL};
		daslog_setHook(&hook);
		daserr_setDisp(DASERRDISP_EXIT);
		atexit(_childAtExit);
		daserrf(DASERR_UNITS, "time to go");
		_exit(98);   /* not reached */
	}

	close(aPipe[1]);
	int aRes[2] = {0, 0};
	ssize_t nRead = read(aPipe[0], aRes, sizeof(aRes));
	close(aPipe[0]);
	int nStatus = 0;
	waitpid(pid, &nStatus, 0);

	CHECK(WIFEXITED(nStatus) && (WEXITSTATUS(nStatus) == DASERR_UNITS),
	      "exit status %d, expected %d", WEXITSTATUS(nStatus), DASERR_UNITS);
	CHECK(nRead == (ssize_t)sizeof(aRes), "atexit report missing");
	CHECK(aRes[0] == DASLOG_CRITICAL, "exit mode error logged at level %d", aRes[0]);
	CHECK(aRes[1] == 2, "%d messages reached the hook, expected the error "
	      "and the atexit warning", aRes[1]);
}

#endif

/* ************************************************************************* */

int main(int argc, char** argv)
{
	(void)argc;
	DasLogHook hook = {_capture, &g_cap};

	/* Return mode with a level that hides errors is refused, and the hook
	   is in place in time to hear why */
	_reset();
	int nRet = daslib_init(argv[0], DASERRDISP_RET, DASLOG_CRITICAL, &hook);
	CHECK(nRet == DASERR_INIT, "daslib_init returned %d", nRet);
	CHECK((g_cap.nCalls == 1)&&(g_cap.nErr == DASERR_INIT), "refusal reported");

	nRet = daslib_init(argv[0], DASERRDISP_RET, DASLOG_INFO, &hook);
	CHECK(nRet == DAS_OKAY, "daslib_init returned %d", nRet);
	CHECK(daserr_disp() == DASERRDISP_RET, "disposition %d", daserr_disp());

	testErrors();
	testLog();
	testLevels();
	testHooks();
	testReentry();
#ifdef HAVE_FORK
	testExit();
#endif

	daslog_setHook(NULL);
	if(g_fails > 0){
		printf("ERROR: %d error and log checks failed\n", g_fails);
		return 13;
	}
	printf("INFO: All error and log checks passed\n");
	return 0;
}
