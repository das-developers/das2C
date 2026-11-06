# Moving a program to libdas3

What a program written against libdas2 (v2.3) has to change to build against
libdas3, biggest change first.

Including `legacy/das2_compat.h` after the das3 headers turns most removed
names into compiler errors that spell out the fix, for example
`das_get_error_removed__save_errors_in_your_log_hook`.  Names that kept their
meaning under a new spelling compile through it unchanged.


## 1. Initialization, errors and logging

Errors and log messages now go one way: through a single log hook.
`das_error()` sends its message to the hook and the library keeps no copy of
it.  The error disposition decides what happens next.

**`das_init()` takes a hook, not a buffer size, and returns a code**

```c
/* v2.3 */
das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

/* das3 */
das_init(argv[0], DASERR_DIS_EXIT, DASLOG_INFO, NULL);
```

The last argument is a `const das_loghook*`, NULL for the default handler
that prints to standard error.  Old five argument calls no longer compile.
`das_init()` returns `DAS_OKAY` or, when the library returns on errors,
`DASERR_INIT`.  It no longer calls `exit()` on its own; a failure follows the
error disposition like any other error and is reported through the hook,
which is installed first.

`das_init()` refuses `DASERR_DIS_RET` with a log level above `DASLOG_ERROR`,
since that hides every error message.  A program that really wants it can
call `daslog_setlevel(DASLOG_CRIT)` afterwards.

**No saved error messages.**  `das_save_error()`, `das_get_error()`,
`das_error_free()`, `das_print_error()` and `das_error_msg` are gone.  A
program that needs the text of an error after the call that raised it, a
language binding raising an exception for example, saves it in its hook:

```c
typedef struct { char sErr[DASLOG_MSG_SZ]; } capture_t;

static void onLog(const das_logmsg* pMsg, void* pUser){
   capture_t* pCap = (capture_t*)pUser;
   if(pMsg->nErr != DAS_OKAY)          /* came from das_error() */
      snprintf(pCap->sErr, DASLOG_MSG_SZ, "%s", pMsg->sMsg);
   else
      daslog_stderr(pMsg, NULL);       /* the rest, as usual */
}

static capture_t g_cap;
das_loghook hook = {onLog, &g_cap};
das_init("mymodule", DASERR_DIS_RET, DASLOG_INFO, &hook);
```

Clear `sErr` before each call into the library and it holds that call's
error, if any.  `das_logmsg` carries the level, the error code (0 for a
plain log message), the text, and the source file, function and line as
separate members.  The text no longer has the location appended.

**Log handlers take a message structure and a user pointer.**  The handler
type is now `void (*)(const das_logmsg* pMsg, void* pUser)`, installed with
its pointer as a pair:

| v2.3 | das3 |
|------|------|
| `daslog_sethandler(fn)` | `daslog_sethook(&hook)`, returns the previous hook |
| `das_log_include_time(b)` | `daslog_showtime(b)` |
| `daslog_set_showline(level)` | `daslog_showline(b)`, ERROR and CRITICAL only |
| `daslog(level, file, line, fmt, ...)` | `daslog(level, file, func, line, fmt, ...)` |

The macros (`daslog_info_v()` and friends) are unchanged.  The plain ones,
`daslog_info(M)` and so on, no longer treat M as a format string.

A handler runs synchronously on the thread that logged, one call at a time.
It must forward the message and return: no das2C calls other than
`daslog_stderr()`, no `exit()`, no `longjmp()`, and no waiting on a lock that
a das2C-calling thread may hold.  Cleanup before an error ends the program
belongs in an `atexit()` function, which runs after the hook has returned
and may use the library.

**Two dispositions.**

| v2.3 | das3 |
|------|------|
| `das_exit_on_error()` | `daserr_setdisp(DASERR_DIS_EXIT)` |
| `das_return_on_error()` | `daserr_setdisp(DASERR_DIS_RET)` |
| `das_error_disposition()` | `daserr_disp()` |
| `das_abort_on_error()`, `DASERR_DIS_ABORT` | removed, set a debugger breakpoint on `daserr_func` |

With `DASERR_DIS_EXIT`, the default, `das_error()` logs at `DASLOG_CRIT` and
then calls `exit()` with the error code.  With `DASERR_DIS_RET` it logs at
`DASLOG_ERROR` and returns the code.

**`DASLOG_NOTHING` is gone.**  `DASLOG_CRIT` is the top level, so a message
that ends the program is always delivered.  To silence the library entirely,
install a hook that drops messages.  `daslog_strlevel()` returns -1 for text
it doesn't understand, and `daslog_setlevel()` ignores a bad level with a
warning and returns the current one.


## 2. Units are input, and input can be bad

`Units_fromStr()` logs text it can't parse at warning level and returns
NULL, it no longer calls `das_error()`.  Check the result.  Passing NULL is
an error; use `""` or `UNIT_DIMENSIONLESS` for no units.  Units strings are
limited to 127 bytes.

`Units_toStr()` no longer turns NULL into dimensionless, and
`Units_toLabel()` treats NULL as an error.  NULL `das_units` were never set,
or something failed upstream; decide what to print before calling either.

`DasPlane_setUnits()` returns a code, and NULL or unparseable units are an
error that leaves the plane unchanged.

When reading a stream, units that don't parse become dimensionless and the
original text is kept in a `unitsText` property (`yUnitsText` for das2
`yUnits`), so passing a stream through loses nothing, but does put un-parsable
units in a spot you might not expect.
