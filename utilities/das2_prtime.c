/* Copyright (C) 2011-2017 Larry Granroth <larry-granroth@uiowa.edu>
 *                         Chris Piker <chris-piker@uiowa.edu>
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


/* ----------------------------------------------------------------------

  prtime.c rewritten by L. Granroth  2011-06-23
  as a simple utility to print a normalized time string given a parseable
  string on either the command line or a list of times on stdin.
  
  Updates by CWP to add extra output options

  Needs libdas.a (-ldas) which includes parsetime, and tnorm.

  cc -O -o prtime prtime.c -I/usr/local/include -L/usr/local/lib -ldas -lm

  prtime February 21, 1960
  1960-02-21 (052) 00:00:00

  --------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <das2/das1.h>


/* ------------------------------------------------------------------------- */
void prn_help(void)
{
	fprintf(stderr, 
"SYNOPSIS\n"
"   das1_prtime - Print an input time string in a standardized format\n"
"\n"
"USAGE\n"
"   das1_prtime [options] [TIME_STRING]\n"
"\n"
"DESCRIPTION\n"
"   das1_prtime parses user date-time strings using the parsetime hueristic\n"
"   and outputs a standardized time string.  The program assumes all input\n"
"   arguments, except those beginning with a minus sign, '-', are time\n"
"   components and will concatenate these together as the user input.  Thus\n"
"   there is no need to wrap space delimited time strings in quotation marks.\n"
"   The default output is the pattern:\n"
"\n"
"      yyyy-mm-dd (DOY) hh:mm:ss\n"
"\n"
"   Where DOY is a three digit day of year, and the reset of the pattern\n"
"   should be self explanatory.  Various command line option alter the\n"
"   default output pattern\n"
"\n"
"   If no TIME_STRING is provided on the command line and -i is not specified\n"
"   then the program exits with an error.\n"
"\n"
"EXIT STATUS:\n"
"   If all input time strings were parseable and all option switches were\n"
"   legal, 0 is return.  If -h or --help is specified, 0 is returned as \n"
"   well.  All other states return a non-zero value.\n"
"\n"
"OPTIONS\n"
"\n"
"   -i    Read times one line at a time from standard input instead of\n"
"         from the command line.  Assumes each line of input contains a\n"
"         single time string.\n"
"\n"
"   -s    Output an ISO-8601 combined date and time string with out the\n"
"         time zone specifier.  These strings have the pattern: \n"
"\n"
"                yyyy-mm-ddThh:mm:ss\n"
"\n"
"   -o    Output an ISO-8601 ordinal date and time string with out the\n"
"         time zone specifier.  These strings have the pattern:\n"
"\n"
"                yyyy-dddThh:mm:ss\n"
"\n"
"   -1 through -9\n"
"         Append N digits of fractional seconds resolution to the output\n"
"         time string\n"
"\n"
"AUTHORS\n"
"   larry-granroth@uiowa.edu (original)\n"
"   chris-piker@uiowa.edu (small additions)\n"
"\n"
"BUGS\n"
"    Not really a bug, but handling time formatting parameters al la \n"
"    strftime would be a handy addition.\n"
"\n"
"SEE ALSO\n"
"   das1_inctime\n"
	);
}

/* ------------------------------------------------------------------------- */
int arg_err(const char* sArg)
{
	fprintf(stderr, "ERROR: In argument '%s'\n", sArg);
	return 4;
}

/* ------------------------------------------------------------------------- */

#define BOTH 0
#define ISOC 1
#define ISOD 2

/* returns non-zero on error */

void prn_time(
	int type, int frac, int year, int month, int dom, int doy, 
	int hour, int minute, double second)
{
	char out[80] = {'\0'};
	char fmt[32] = {'\0'};
	int printed = 0;
	double nsec = floor(second);
	double fsec = second - nsec;
	int i, nPower;
	
	switch(type){
	case BOTH:
		printed = snprintf(out, 79, "%04d-%02d-%02d (%03d) %02d:%02d:%02d",
		                   year, month, dom, doy, hour, minute, (int)nsec);
		break;
	case ISOC:
		printed = snprintf(out, 79, "%04d-%02d-%02dT%02d:%02d:%02d",
		                   year, month, dom, hour, minute, (int)nsec);
		break;
	case ISOD:
		printed = snprintf(out, 79, "%04d-%03dT%02d:%02d:%02d",
		                   year, doy, hour, minute, (int)nsec);
		break;
	default:
		exit(13);
	}
	
	if(frac > 9) {
		fprintf(stderr, "ERROR: Number of fractional digits must be less than 10\n");
		exit(13);
	}
	
	if(frac == 0){
		out[printed] = '\n';
	}
	else{
		snprintf(fmt, 31, ".%%0%dd\n", frac);
		nPower = 1;
		for(i = 0; i<frac; i++) nPower *= 10;
		snprintf(out + printed, 79 - printed, fmt, (int) (fsec*nPower));
	}
	fputs(out, stdout);
}

/* ------------------------------------------------------------------------- */

int main (int argc, char *argv[])
{
	int year, month, day_month, day_year, hour, minute;
	double second;
	char s[80] = {'\0'};
	int len, i;
	int type = BOTH;
	int frac = 0;  /* Number of fractional digits */
	int bStdIn = 0;
	
	/* First troll the arg list looking for -h or --help */
	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0){
			prn_help();
			return 0;
		}
	}
   
	/* assemble time string from individual arguments, if necessary, skip */
	/* anything starting with '-' */
	i = 1; len = 0;
	while(i < argc && len < 79) {
		if(argv[i][0] != '-'){
			s[len] = ' '; 
			len++;
			s[len] = '\0';
			strncat (s+len, argv[i], 79 - len);
			len = strlen(s);
		}
		i++;
	}
	s[79] = '\0';
	
	
	/* Check for output formatting arguments */
	i = 1;
	while(i < argc){
		if(argv[i][0] == '-'){
			switch(argv[i][1]){
			case 's':  type = ISOC; break;
			case 'o':  type = ISOD; break;
			case '0': case '1': case '2': case '3': case '4': 
			case '5': case '6': case '7': case '8': case '9':
				frac = atoi(argv[i]+1);
				break;
			case 'i': bStdIn = 1; break;
			
			default: /* Works if null is found right away too */
				return arg_err(argv[i]);
			}
		}
		i++;
	}
	
	/* If nothing on command line, and we're not supposed to read */
	/* stdard input, exit with error */
	if(s[0] == '\0' &&  bStdIn == 0){
		fprintf(stderr, "ERROR: No input data on command line and not "
				  "reading standard in.\n");
		return 4;
	}
	
	do {

		/* skip zero or short strings */
		if (strlen(s) > 1) {

			/* parse time string */
			if(parsetime(s, &year, &month, &day_month, &day_year,
			             &hour, &minute, &second)) {
				fprintf (stderr, "%s: error parsing \"%s\"\n", argv[0], s);
				return 127;
			}
		
			/* and spew the result */
			prn_time(type, frac, year, month, day_month, day_year, hour, 
					   minute, second);
		} /* if at least 2-character string */

		/* if no command line, then try to read a line from standard input */

  } while (bStdIn && (fgets(s, 80, stdin) != NULL));

  return 0;

}
