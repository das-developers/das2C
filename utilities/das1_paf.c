/* print all fields for any das data stream  */


/* TODO:  Combine with das1_prtime.c */

/* WARNING: 
 * This program only works with das1 streams that do not gave packet headers. 
 */

#include <stdio.h>

int
main (int argc, char *argv[])
{
  int i;
  int items;
  float *buf;
  

  if (argc != 2) return -1;
  if (!sscanf (argv[1], "%d", &items)) return -1;

  buf = (float *) malloc (items * sizeof(float));
  if (!buf) return -1;

  while (fread (buf, items * sizeof(float), 1, stdin)) {

    printf ("%15.8e", buf[0]);
    for (i = 1; i < items; i++) printf ("%10.3e", buf[i]);
    printf ("\n");

  }

  free (buf);
  return 0;

}
