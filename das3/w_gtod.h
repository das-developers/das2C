/* Replacement for gettimeofday for Windows
 *
 * The usual FILETIME to Unix time conversion: count 100 ns ticks since
 * 1601-01-01, subtract the ticks up to 1970-01-01, divide down to seconds.
 * Written here around 2015, likely with a web search open, but not copied
 * from anywhere in particular, so no license notice is carried.
 *
 * Known limits, left alone for now:
 *
 *   - Millisecond resolution only.  tv_usec comes from SYSTEMTIME's
 *     wMilliseconds, though FILETIME counts 100 ns ticks.  To fix, take
 *     tv_usec from (time - EPOCH) % 10000000 / 10, and read the clock with
 *     GetSystemTimeAsFileTime(), or GetSystemTimePreciseAsFileTime() on
 *     Windows 8 and up, which also skips the SYSTEMTIME round trip.
 *
 *   - struct timeval's members are long, 32 bits on Windows, so tv_sec runs
 *     out in January 2038.  That's winsock's struct, not this function.
 */

#include <stdint.h>

static int win_gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970 
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
}
