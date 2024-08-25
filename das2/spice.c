#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>

#include <SpiceUsr.h>

#include "spice.h"
#include "array.h"
#include "send.h"

void das_spice_err_setup()
{
	errprt_c("SET", 4, "ALL");   /* Want all error text */
	erract_c("SET", 7, "RETURN");/* Just return on error, we'll check for it */
	errdev_c("SET", 4, "NULL");  /* Don't go printing to the screen on error */
}

void strfort2c(char* s, int nLen)
{
	char* p = s + nLen - 1;
	while(p >= s){
		if((*p != ' ')&&(*p != '\0')) break;
		if(*p == ' ') *p = '\0';
		p--;
	}
}

int das_send_spice_err(int nDasVer, const char* sErrType)
{
	char sMsg[1842] = {'\0'};
	char sMsgEsc[2048] = {'\0'};
	char sOut[3072] = {'\0'};
	int iNext = 0; 
	
	getmsg_c("SHORT", 40, (char*)sMsg);
	
	/* Convert from fortran right-space padding to C-null termination */
	strfort2c(sMsg, 41);
	
	iNext = strlen(sMsg);
	sMsg[iNext] = ' ';
	iNext++;
	getmsg_c("LONG", 1841 - iNext, ((char*)sMsg)+iNext);
	
	strfort2c(sMsg, 1842);
		
	fprintf(stderr, "ERROR: %s\n", sMsg);

	
	if(nDasVer > 1){
		_das_escape_xml(sMsgEsc, 2047, sMsg);

		if(nDasVer == 2){

			snprintf(sOut, 3071, 
				"<exception type=\"%s\"\n"
				"           message=\"%s\" />\n", sErrType, sMsgEsc
			);
			printf("[XX]%06zu%s", strlen(sOut), sOut);
		}
		else{
			if(nDasVer == 3){
				snprintf(sOut, 3071, 
				"<exception type=\"%s\">\n"
				"%s\n"
				"</exception>\n", sErrType, sMsgEsc
			);
			printf("[XX]%06zu%s", strlen(sOut), sOut);
			}
		}
		das_error(DASERR_SPICE, "Unknown stream version %d", nDasVer);
	}
	
	return DASERR_SPICE;
}

int das_print_spice_error(const char* sProgName)
{
	char sMsg[1842] = {'\0'};
	int iNext = 0; 
	
	getmsg_c("SHORT", 40, (char*)sMsg);
	
	/* Convert from fortran right-space padding to C-null termination */
	strfort2c(sMsg, 41);
	
	iNext = strlen(sMsg);
	sMsg[iNext] = ' ';
	iNext++;
	getmsg_c("LONG", 1841 - iNext, ((char*)sMsg)+iNext);
	
	strfort2c(sMsg, 1842);
		
	if(sProgName) fprintf(stderr, "ERROR (%s): %s\n", sProgName, sMsg);
	else fprintf(stderr, "ERROR: %s\n", sMsg);
	
	return DASERR_SPICE;	
}

DasErrCode das_spice_dm2et(double* pEt, const das_datum* pDatum)
{
	/* TODO: Add in hook to pull from variable if needed for sequences, etc. ! */

	return das_error(DASERR_NOTIMP, "Time to et conversion not yet implemented");

	/* Not yet implemented ...
	das_val_type vt = DasAry_valType(pCalc->pSrc);
	das_units units = DasAry_units(pCalc->pSrc);
	const ubyte* pData = DasAry_getAt(pCalc->pSrc, vt, aShape);

	if(pCalc->request.uFlags & XFORM_LOC){

		/ * Time usually comes in one of three forms:
		   1. double, units
		   2. long, units
		   3. das_time, implied UTC

		Note that spice times don't have the same precision as TT2K over the 
		TT2K time range.  There's nothing to be done about this other then not
		calculating ephemeris vectors for every single point in a 20 MHz waveform!
		* /
		double et;
		switch(vt){
		case vtDouble:
			
			
		}




		/ * Extract the time values * /
		das_datum dm;
		DasVar_get(pTime, [0,0,0,0]);

		/ * need a fast converter from TT2K to ephemeris time * /
		double et = das_tt2K_to_ephem(*(int64_t*)(&dm));
	}

#ifdef DEBUG
		/ * Now check it * /
		das_time dt;
		dt_from_tt2k()

#endif	

   */
}