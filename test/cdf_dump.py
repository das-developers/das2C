#!/usr/bin/env python3

import sys
import das2.pycdf

f = das2.pycdf.CDF(sys.argv[1])

for k in f.attrs:
	print(k, "=", f.attrs[k])
	
print()

for k in f.keys():
	print(k)
	print(f[k])
	print(f[k].attrs)
	
	# Now print the first record
	print( f[k].shape)
	if len(f[k].shape) == 1:
		print( f[k][:] )
	else:
		if f[k].shape[0] > 0:
			print( f[k][0,:] )
	
	print()
	

