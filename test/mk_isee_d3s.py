#!/usr/bin/env python3

"""Make d3s test streams from ISEE rapdi-sample spectra, which have
   an odd shape.  This throw-away script assumes the input data don't
	have gaps... for the first 3 full runs of the sweep set anyway.
"""

g_sDir = '/project/isee/data/1/pwi/cdf/1984/09/07'
g_sFile = 'isee1_pwi_sa-rapid-e_1984-09-07_v1.cdf'

import das2
from das2.pycdf import CDF
from os.path import join as pjoin
from datetime import datetime
import numpy as np
import calendar
import struct

def writeHdr(fOut, sType, sHdr):
	xHdr = sHdr.encode('utf-8')
	nLen = len(xHdr)
	
	if sType == 'Sx':
		xTag = ('|Sx||%d|'%nLen).encode('utf-8')
	elif sType == 'Hx':
		xTag = ('|Hx|1|%d|'%nLen).encode('utf-8')
	else:
		raise ValueError("Unknown pkt type %s"%sType)

	fOut.write(xTag + xHdr)
	fOut.flush()


def writePkt(fOut, xBytes):
	nLen = len(xBytes)
	xTag = ('|Pd|1|%d|'%nLen).encode('utf-8')
	
	fOut.write(xTag + xBytes)
	fOut.flush()


dIn = CDF(pjoin(g_sDir, g_sFile))

fRank1 = open('ex24_isee_rapid_rank1.d3b','wb')
fRank2 = open('ex25_isee_rapid_rank2.d3b','wb')
fRank3 = open('ex26_isee_rapid_rank3.d3b','wb')

sStrm = '''
<stream type="das-basic-stream" version="3.0" />
'''

sHdr1 = '''
<dataset name="sa_rapid" rank="1" index="*">
  <properties><p name="info">
    ISEE-1 PWI Spectrum Analyzer Rapid Samples as Scatter Data
    This version uses the most space.
  </p></properties>
  <coord physDim="time" axis="x">
    <scalar semantic="datetime" index="*" units="t1970">
      <packet numItems="1" itemBytes="8" encoding="LEreal" />
    </scalar>
  </coord>
  <coord physDim="frequency" axis="y">
    <scalar semantic="real" index="*" units="Hz">
      <packet numItems="1" itemBytes="4" encoding="LEreal" />
    </scalar>
  </coord>
  <data physDim="ESD" name="e_series">
    <scalar semantic="real" index="*" units="V**2 m**-2 Hz**-1" >
      <packet numItems="1" itemBytes="4" encoding="LEreal" />
    </scalar>
  </data>
</dataset>
'''

sHdr2 = '''
<dataset name="sa_rapid" rank="2" index="*;128">
  <properties><p name="info">
    ISEE-1 PWI Spectrum Analyzer Rapid Samples as fixed frequency packets
    Each run in 'j' is a set of captures at a single frequency. 
    This drops number of bytes streamed per value from roughly
      
      `10 + 8 + 4 + 4 = 26` to `4` 
         
     since a packet tag, a time, and a freq. are only needed once
     every 128 values.
  </p></properties>
  <coord physDim="time" axis="x">
    <scalar use="reference" semantic="datetime" index="*;-" units="t1970">
      <packet numItems="1" itemBytes="8" encoding="LEreal" />
    </scalar>
    <scalar use="offset" semantic="real" index="-;128" units="s">
      <sequence minval="0" interval="0.125" />
    </scalar>
  </coord>
  <coord physDim="frequency" axis="y">
    <scalar semantic="real" index="*;-" units="Hz">
      <packet numItems="1" itemBytes="4" encoding="LEreal" />
    </scalar>
  </coord>
  <data physDim="ESD" name="e_series">
    <scalar semantic="real" index="*;128" units="V**2 m**-2 Hz**-1" >
      <packet numItems="128" itemBytes="4" encoding="LEreal" />
    </scalar>
  </data>
</dataset>
'''

sHdr3 = '''
<dataset name="sa_rapid" rank="3" index="*;16;128">
  <properties><p name="info">
    ISEE-1 PWI Spectrum Analyzer Rapid Samples as full sweep blocks
    
    Each run in 'j' is a full frequency sweep.
    Each run in 'k' is a set of captures at a single frequency.
   
    This is slightly more efficent then the rank2 example but more
    importantly organizing data in this way allows clients to
    trivially slice the dataset in frequency.  All major changes
    in the  dataset coordinates are associated with an array axis.
    
    The downside, is time offsets need to be a function of two
    indicies, and any partial packets need to be filled out to
    the full 256 second sweep.
    
  </p></properties>
  <coord physDim="time" axis="x">
    <scalar use="reference" semantic="datetime" index="*;-;-" units="t1970">
      <packet numItems="1" itemBytes="8" encoding="LEreal" />
    </scalar>
    <scalar use="offset" semantic="real" index="-;16;128" units="s">
      <sequence minval="0" interval="16;0.125" />
    </scalar>
  </coord>
  <coord physDim="frequency" axis="y">
    <scalar semantic="real" index="-;16;-" units="Hz">
      <values>
        56.2 100.0 178.0 311.0 
        562.0 1000.0 1780.0 3110.0
        5620.0 10000.0 17800.0 31100.0
        56200.0 100000.0 178000.0 311000.0
      </values>
    </scalar>
  </coord>
  <data physDim="ESD" name="e_series">
    <scalar semantic="real" index="*;16;128" units="V**2 m**-2 Hz**-1" >
      <packet numItems="2048" itemBytes="4" encoding="LEreal" />
    </scalar>
  </data>
</dataset>
'''

g_lFreq = [
	56.2,100.0, 178.0, 311.0,
   562.0, 1000.0, 1780.0, 3110.0,
	5620.0, 10000.0, 17800.0, 31100.0,
	56200.0, 100000.0, 178000.0, 311000.0
]

writeHdr(fRank1, 'Sx', sStrm)
writeHdr(fRank1, 'Hx', sHdr1)

writeHdr(fRank2, 'Sx', sStrm)
writeHdr(fRank2, 'Hx', sHdr2)

writeHdr(fRank3, 'Sx', sStrm)
writeHdr(fRank3, 'Hx', sHdr3)


# This would never be used in production, it can be slow since
# it generates a test set.


aEpoch = np.array([
	das2.DasTime(dt).epoch('t1970') for dt in dIn['Epoch'][:]
], dtype='float64')

aFreq  = dIn['Frequency'][:]
aDat    = dIn['E_Series'][:]
nLen   = aEpoch.shape[0]

idxR1 = [None]
idxR2 = [None, None]
idxR3 = [None, None, None]

bRead = False
i0 = None
iR2Last = 0
iR3Last = 0
for i in range(1, nLen):
	
	if not i0:
		# Find the index after the first high to low transition
		if (aFreq[i-1] == g_lFreq[-1]) and (aFreq[i] == g_lFreq[0]):
			i0 = i
		else:
			continue
		
	# Compute indicies for each of the different ranks
	idxR1[0] = i - i0
	
	idxR2[0] = (i - i0) // 128
	idxR2[1] = (i - i0) % 128
	
	idxR3[0] = (i - i0) // (16*128)
	
	nRem = (i - i0) % (16*128)
	
	idxR3[1] = nRem // 128
	idxR3[2] = nRem % 128
	
	# DEBUG: Index testing
	#print("%d (%.3f, %8.1f) %5d  [%5d,%5d]  [%5d,%5d,%5d]"%(
	#	i, aEpoch[i], aFreq[i], idxR1[0], idxR2[0], idxR2[1], 
	#	idxR3[0], idxR3[1], idxR3[2]
	#))
		
	# Emit high-rank packets if we're past thier last value
	if idxR2[0] > iR2Last: 
		
		xCoord = struct.pack("<df", aEpoch[i - 128], aFreq[i])
		xData = aDat[i-128:i].astype('<f4').tobytes() 
		writePkt(fRank2, xCoord + xData)
		
	if idxR3[0] > iR3Last:
	
		xCoord = struct.pack("<d", aEpoch[i - 2048])
		xData = aDat[i-2048:i].astype('<f4').tobytes() 
		writePkt(fRank3, xCoord + xData)
		
		# We only need to write 3 top-level packets
		if idxR3[0] > 2:
			break  # Don't write the block+1 rank1 packet below...
	
	# Always emit the low rank packet
	x = struct.pack("<dff", aEpoch[i], aFreq[i], aDat[i])
	writePkt(fRank1, x)

	iR2Last = idxR2[0]
	iR3Last = idxR3[0]
	












