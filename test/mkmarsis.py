#!/usr/bin/env python2
import struct
import sys

pout = sys.stdout.write

f = file('FRM_AIS_RDR_18874.DAT', 'rb')

lRecs = []
for i in range(0,3):
	lRecs.append(f.read(400*160))


# Get the three record times
lTimes = [ r[24:24+21] for r in lRecs]
pout("const char* lTimes[3] = {NULL};  /* Ionogram times */\n")
for i in range(0,3):
	pout('lTimes[%d] = "%s";\n'%(i,lTimes[i]))
pout("\n\n");


# Get the frequencies
tFreq = [ struct.unpack('>f', lRecs[0][400*i+76:400*i+80])[0] for i in range(0,160)]

pout('const float g_aFreq[160] = { /* Pulse frequencies in Hz */\n\t')
for i in range(0,160):
	if i % 8 == 7:
		if i < 159: pout("%7.0f,\n\t"%tFreq[i])
		else: pout("%7.0f\n\t"%tFreq[i])
	else:
		pout("%7.0f,"%tFreq[i])

pout('};\n\n')

# You just have to get this from AISDS.CAT, very annoying
#  delay time (microseconds) = 167.443 + 91.4286 * (item-1)
# lDelays = [167.443 + 91.4286 * i for i in range(0,160)]
pout('''/* Delay times in microseconds calculated using AISDS.CAT */
double g_aDelay[80] = {0.0};
void init_delay(void){
	for(int i = 0; i < 90; ++i) g_aDelay[i] = 167.443 + 91.4286 * i;
}

''')

pout('''/* Apparent range in km assuming free space propogation */
double g_aAppRng[80] = {0.0};
void init_apprng(void){
	double C =  299792458.0 * 1.0e-9;  /* in km/microsecond */
	for(int i = 0; i < 90; ++i)
		g_aAppRng[i] = g_aDelay[i] * 0.5 * C;  /* two way light time */
}

''')

# Print the spectral densities
pout('const float g_aAmp[3][160][80] = {  /* Radar returns (V**2 m**-2 Hz**-1) */\n');


for iRec in range(0,3): # Records
	pout('{  /* Pulse Set @ %s */'%lTimes[iRec])
	
	for iFreq in range(0,160): # Freq
		pout('\n\t{ /* Returns for the %7.0f Hz Pulse */'%tFreq[iFreq])
		iFreqIdx = iFreq*400 + 80
		
		#print(iRec, iFreq, len(lRecs[iRec][iFreqIdx : iFreqIdx+320]))
		
		tAmp = struct.unpack(">80f", lRecs[iRec][iFreqIdx : iFreqIdx+320])
		
		for iRow in range(0,10): # Delay row
			pout('\n\t\t')
			
			for iCol in range(0,8): #delay column
				iAmp = iRow*8 + iCol
				if (iRow == 9) and (iCol == 7):
					pout('%.2e'%tAmp[iAmp])
				else:
					pout('%.2e,'%tAmp[iAmp])
		
		if iFreq < 159: pout('\n\t},')  # End amplitudes
		else:           pout('\n\t}')
	
	if iRec < 2: pout('\n},') # End Pulse Set  (i.e. record) 
	else:        pout('\n}') 

pout('\n};\n\n') # End Records

	


