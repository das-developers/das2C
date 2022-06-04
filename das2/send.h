/** @file send.h - Das Reader output formatting helpers
 *
 * The das core library libdas2.a supplies a full API for generating Das2 
 * streams, however these streams are relativily simple and don't really
 * require a heavy weight library for applications that only write a fixed
 * das2 stream type.  Most readers fall into this use case.  
 * 
 * These functions supply a few fixed functions to assist Das2 readers.
 */

#ifndef _das2_outbuf_H_
#define _das2_outbuf_H_

#include <stdarg.h>
#include <stdlib.h>

/** Send a stub stream header. 
 *
 * All das2 streams must start with a stream header.  This function sends a
 * minimal stream header which is just good enough to be the prefix for an
 * error message.  Only call this if your program needs to output an error
 * message before it's sent it's own stream header.
 */
void das_send_stub(int nDasVer);

/** Output a no data in interval message 
 * @returns the integer 0
 */
int das_send_nodata(int nDasVer, const char* sFmt, ...);

/** Output a "user-messed up" message when receiving a badly formed query 
 * @returns the integer 47
 */
int das_send_queryerr(int nDasVer, const char* sFmt, ...);

int das_vsend_queryerr(int nDasVer, const char* sFmt, va_list argp);

/** Output a server problem message (i.e. missing spice kernel, etc) 
 * @returns the integer 48
 */
int das_send_srverr(int nDasVer, const char* sFmt, ...);

/** Output a log status message (i.e. reading file T120101.DAT) */
void das_send_msg(int nDasVer, const char* source, const char* sFmt, ...);

/** Start a progress bar on the recieving client 
 *
 * @warning Not thread safe, only use from your writing thread
 *
 * This function keeps track of the last progress update and will not issue
 * a new one unless at least 1% more progress has been made
 *
 * @param nDasVer the das stream version number you're writing, either 1 or 2
 * @param sSrc The name of the thing setting the progress bar
 * @param fBeg some begin mark, can be any floating point number.
 * @param fEnd some end mark, can be any floating point number bigger than fBeg
 */
void das_send_progbeg(int nDasVer, const char* sSrc, double fBeg, double fEnd);

/** Update a progress bar on the receiving client 
 * @warning Not thread safe, only use from your writing thread
 *
 * @param nDasVer the das stream version number you're writing, either 1 or 2
 * @param sSrc The name of the thing setting the progress bar
 * @param fCurrent some floating point number between the start and end marks
 *        set in das_send_progbeg().
 */
void das_send_progup(int nDasVer,  const char* sSrc, double fCurrent);



#ifdef HOST_IS_LSB_FIRST
/** Macro to invoke byte swaping only on little endian machines 
 * This is useful for Das1 readers that must output all data in big endian
 * format */
#define das_msb_float(x) _das_swap_float(x)
#else
#define das_msb_float(x) x
#endif

float _das_swap_float(float rIn);

void _das_escape_xml(char* sDest, size_t uOutLen, const char* sSrc);
		
#endif /* _das2_outbuf_H_ */
