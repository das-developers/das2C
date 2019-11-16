/* Copyright (C) 1996-2017 Larry Granroth <larry-granroth@@uiowa.edu>
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


/*---------------------------------------------------------------------------

  tav.c written by L. Granroth  08-09-96
  as a filter to time average any *das* data stream

  tav items period [fill]

  where items is the total number of floating point items (including time tag)
  and period is the time in seconds to average over.  The third argument,
  if present will be used as the "no data" value which defaults to 0.0.
  Time bins will be aligned with time = 0.0 and the output will be tagged
  with the time of the center of the averaging bins.
  
  ---------------------------------------------------------------------------*/

/* WARNING: 
 * This program only works with das1 streams that do not gave packet headers. 
 */


#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char *argv[])
{
  int i = 0;
  int items;
  double period;
  float fill;
  float *buf, *sum;
  int *num;
  long bin, thebin;
  
  if (argc < 3) return -1;
  if (!sscanf (argv[1], "%d", &items)) return -1;
  if (!sscanf (argv[2], "%lg", &period)) return -1;
  if ((items < 2) || (period <= 0.0)) return -1;

  if (argc > 3) {
    if (!sscanf (argv[3], "%g", &fill)) return -1;
  } else fill = 0.0;

  fprintf (stderr, "items %d period %lf fill %f\n", items, period, fill);

  buf = (float *) malloc (items * sizeof(float));
  if (!buf) return -1;
  sum = (float *) malloc (items * sizeof(float));
  if (!sum) return -1;
  num = (int *) malloc (items * sizeof(int));
  if (!num) return -1;

  thebin = -1;
  num[i] = 0;

  while (fread (buf, items * sizeof(float), 1, stdin)) {

    /* the first element is a time offset in seconds */

    bin = (double)buf[0] / period;

    if (bin != thebin) {

      /* calculate and write the averages */

      if (num[0] != 0) {
        for (i = 1; i < items; i++) if (num[i]) sum[i] = sum[i] / num[i];
	if (!fwrite (sum, items * sizeof(float), 1, stdout)) return -1;
      }

      /* reset for the next bin */

      thebin = bin;
      for (i = 0; i < items; i++) {
        num[i] = 0;
	sum[i] = 0.0;
      }
      sum[0] = ((double)thebin + 0.5) * period;

    } /* if new bin */

    if (bin >= 0) {

      (num[0])++;
      for (i = 1; i < items; i++) {
        if (buf[i] != fill) {
	  (num[i])++;
	  sum[i] += buf[i];
	}
      }

    } /* if positive bin */

  }

  /* flush any remaining data */

  if (num[0] != 0) {
    for (i = 1; i < items; i++) if (num[i]) sum[i] = sum[i] / num[i];
    if (!fwrite (sum, items * sizeof(float), 1, stdout)) return -1;
  }

  free (buf); free (sum); free (num);
  return 0;

}
