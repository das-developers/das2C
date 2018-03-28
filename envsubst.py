#!/usr/bin/env python

import os
import sys
from os.path import basename as bname
from os.path import dirname as dname


perr = sys.stderr.write

def prn_help():
	perr("\n")
	perr("Substitute named string format items in a file with values from the\n")
	perr("environment.\n")
	perr("\n")
	perr("Usage: %s INFILE OUTFILE\n"%os.path.basename(sys.argv[0]))
	perr("\n")
	
if len(sys.argv) < 3:
	prn_help()
	sys.exit(13)

if bname(sys.argv[2]) != sys.argv[2]:
	if not os.path.isdir(dname(sys.argv[2])):
		os.makedirs(dname(sys.argv[2]))


fIn = file(sys.argv[1], 'rb')
fOut = file(sys.argv[2], 'wb')

sFmt = fIn.read()
fIn.close()

fOut.write(sFmt%os.environ)

fOut.close()

sys.exit(0)

