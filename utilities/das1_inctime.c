/* ----------------------------------------------------------------------

  inctime.c written by L. Granroth  01-28-98
  as a simple utility to increment a time string by a number of seconds.

  Needs libdas.a (-ldas) which includes parsetime, and tnorm.

  cc -O -o inctime inctime.c -I/local/include -L/local/lib -ldas -lm

  inctime "February 21, 1960" 86400
  1960-02-22 (053) 00:00:00

  --------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "das2/das1.h"

int
main (int argc, char *argv[])
{
  int year, month, day_month, day_year, hour, minute;
  double second;
  double increment;
  char s[80];
  int len, i;
  char *p;

  if (argc < 3) {
    fprintf (stderr, "usage: %s <time-string> <seconds>\n", argv[0]);
    exit (-1);
  }

  /* last argument must be increment in seconds */
  argc--;
  increment = strtod (argv[argc], &p);
  if (p == argv[argc]) {
    fprintf (stderr, "%s: error parsing increment in seconds: %s\n",
      argv[0], p);
    exit (-1);
  }

  /* assemble time string from individual arguments, if necessary */
  for (i = 1, len = 0; i < argc && len < 79; i++) {
    s[len++] = ' ';
    s[len] = '\0';
    strncat (s+len, argv[i], 79 - len);
    len = strlen (s);
  }

  s[79] = '\0';

  /* parse time string */
  if (parsetime (s, &year, &month, &day_month, &day_year,
    &hour, &minute, &second)) {
    fprintf (stderr, "%s: error parsing %s\n", argv[0], s);
    exit (-1);
  }

  /* do the increment */
  second += increment;

  /* re-normalize the time (always watch for bugs in tnorm!) */
  tnorm (&year, &month, &day_month, &day_year, &hour, &minute, &second);

  /* and spew the result */  
  fprintf (stdout, "%04d-%02d-%02d (%03d) %02d:%02d:%02d\n",
    year, month, day_month, day_year, hour, minute, (int)second);

  exit (0);

}
