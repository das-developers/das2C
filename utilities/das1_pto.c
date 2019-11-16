/* print time offset for any das data stream  */

/* WARNING: 
 * This program only works with das1 streams that do not gave packet headers. 
 */

#include <stdio.h>

int
main (int argc, char *argv[])
{
  int items;
  float *buf;
  
  if (argc != 2) return -1;
  if (!sscanf (argv[1], "%d", &items)) return -1;

  buf = (float *) malloc (items * sizeof(float));
  if (!buf) return -1;

  while (fread (buf, items * sizeof(float), 1, stdin)) {

    printf ("%10.3e\n", buf[0]);

  }

  free (buf);
  return 0;

}
