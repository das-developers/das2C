# -*- coding: utf-8 -*-
import time
import datetime
import sys
import math as M

import _das2

##############################################################################
# Wrapper class for dastimes

class DasTime(object):
	"""A wrapper for the old daslib functions, parsetime and tnorm,
	as well as adding features that let one do comparisons on dastimes
	as well as use them for dictionary keys.
	"""

	@classmethod
	def from_string(cls, sTime):
		"""Static method to generate a DasTime from a string, uses the
		   C parsetime to get the work done"""
		t = _das2.parsetime(sTime)
		return cls(t[0], t[1], t[2], t[4], t[5], t[6])
		
	@classmethod
	def now(cls):
		"""Static method to generate a DasTime for right now."""
		#print t
		t = time.gmtime()
		#print t[6]
		rSec = time.time()
		#print rSec
		fSec = t[5] + (rSec - int(rSec))
		#print fSec
		return cls(t[0], t[1], t[2], t[3], t[4], fSec)
		
		
	dDaysInMon = (
		(0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31),
		(0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
	)		
			
	def __init__(self, nYear=0, nMonth=0, nDom=0, nHour=0, nMin=0, fSec=0.0):
		"""Initalize from field values
		
		Note: If nYear is a string, and all other values are zero, then 
		      parsetime is called to generate the field values from the time
				string.
		"""
		
		if isinstance(nYear, unicode):
			nYear = nYear.encode('utf-8')
		
		if isinstance(nYear, str):
			try:
				(nYear, nMonth, nDom, nDoy, nHour, nMin, fSec) = _das2.parsetime(nYear)
			except ValueError, e:
				raise ValueError("String '%s' was not parseable as a datetime"%nYear)						
		
		elif isinstance(nYear, datetime.datetime):
			pdt = nYear
			nYear = pdt.year
			nMonth = pdt.month
			nDom = pdt.day
			nHour = pdt.hour
			nMin = pdt.minute
			fSec = float(pdt.second) + (pdt.microsecond / 1000000.0)
			
		elif isinstance(nYear, DasTime):
			# Just work as a copy constructor
			nMonth = nYear.t[1]
			nDom = nYear.t[2]
			nHour = nYear.t[4]
			nMin = nYear.t[5]
			fSec = nYear.t[6]
			nYear = nYear.t[0]
			
		
		# Assume years less than 100 but greater than 57 are old 2 digit years
		# that need to be incremented.  I don't like this but what can
		# you do when reading old data.
			
		if isinstance(nYear, (tuple, list)):
			self.t = [0, 0, 0, 0, 0, 0, 0.0]
			for i in xrange(0, len(nYear)):
				j = i
				if i >= 3:
					j = i+1
				self.t[j] = nYear[i]
				
			if self.t[0] < 100 and self.t[0] > 57:
				self.t[0] = self.t[0] + 1900
			
			self.t = list( _das2.tnorm(t[0], t[1], t[2], t[4], t[5], t[6]) )
		else:
			if nYear < 100 and nYear >= 57:
				nYear += 1900
						
			self.t = list( _das2.tnorm(nYear, nMonth, nDom, nHour, nMin, fSec) )
		
		if abs(self.t[0]) > 9999:
			raise OverflowError("Year value is outside range +/- 9999")
	
	def __repr__(self):
		return "DasTime('%s')"%self.__str__()
		        
	def __str__(self):
		"""Prints an ISO 8601 standard day-of-month time string to micorsecond
		resolution."""
		return '%04d-%02d-%02dT%02d:%02d:%09.6f'%(self.t[0], self.t[1],
		           self.t[2], self.t[4], self.t[5], self.t[6])
	
	def __cmp__(self, other):
		if not isinstance(other, DasTime):
			return 1
			
		nCmp = cmp(self.t[0], other.t[0])
		if nCmp != 0:
			return nCmp
			
		nCmp = cmp(self.t[1], other.t[1])
		if nCmp != 0:
			return nCmp

		nCmp = cmp(self.t[2], other.t[2])
		if nCmp != 0:
			return nCmp

		nCmp = cmp(self.t[4], other.t[4])
		if nCmp != 0:
			return nCmp

		nCmp = cmp(self.t[5], other.t[5])
		if nCmp != 0:
			return nCmp
	
		return cmp(self.t[6], other.t[6])
		
	
	def __hash__(self):
		"""Compute a 64-bit hash that is useable down to microsecond
		resolution over the years +/- 9,999.  So, not great for geologic 
		time, nor for signal propagation times on microchips"""
		return self.__long__()
	
	
	def __long__(self):
		"""Cast the dastime to a long value, mostly only good as
		a hash key, but placed here so that you can get the hash easily"""
		
		lHash = self.t[0] * 100000000000000L
		
		# Use doy in the hash to save digits
		lHash += self.t[3] *   100000000000L
		
		nSecOfDay = self.t[4] * 3600
		nSecOfDay += self.t[5] * 60
		nSecOfDay += int(self.t[6])
		
		lHash += nSecOfDay *        1000000L
		
		nMicroSec = int( (int(self.t[6]) - self.t[6] ) * 1000000 )
		
		lHash += nMicroSec
		return lHash
		
	
	def __nonzero__(self):
		"""Used for the boolean 'is true' test"""
		for i in [0,1,2,4,5,6]:
			if self.t[i] != 0:
				return True
		
		return False
		
			
	def norm(self):
		"""Normalize the time fields so that all contain legal values."""
		tNew = _das2.tnorm(self.t[0], self.t[1], self.t[2], self.t[4], self.t[5],
		               self.t[6])
		self.t = list( tNew )
		
	def mj1958(self):
		"""Get the current time value as seconds since January 1st 1958, ignoring
		leap seconds"""
		return _das2.ttime(self.t[0], self.t[1], self.t[2], self.t[4], self.t[5],
		               self.t[6])
							
	def t2000(self):
		return self.mj1958() - _das2.ttime(2000, 01, 01)
		
		
	def adjust(self, nYear, nMonth=0, nDom=0, nHour=0, nMin=0, fSec=0.0):
		"""Adjust one or more of the field, either positive or negative,
		calls self.norm internally
		
		Does *not* return a new DasTime, modifies the given object
		"""
		
		if nYear == 0 and nMonth == 0 and nDom == 0 and nHour == 0 and \
		   nMin == 0 and fSec == 0.0:
			return
			
		t = [self.t[0] + nYear, 
		     self.t[1] + nMonth,
		     self.t[2] + nDom,
		     self.t[4] + nHour,
		     self.t[5] + nMin,
		     self.t[6] + fSec]
			  		
		self.t = list( _das2.tnorm(t[0], t[1], t[2], t[3], t[4], t[5]) )
				
	# I'm sure there is a standard sematic for this, change to that
	# when you find it
	def copy(self):
		return DasTime(self.year(), self.month(), self.dom(), self.hour(), 
		               self.minute(), self.sec())
							
	###########################################################################
	def floor(self, nSecs):
		'''Find the nearest time, evenly divisable by nSec, that is 
		less that the current time value.
		'''
		
		if nSecs - int(nSecs) != 0:
			raise ValueError("%s is not an integer"%nSecs)		
		nSecs = int(nSecs)
		
		if nSecs < 1:
			raise ValueError("%s is < 1"%nSecs)
		
		elif nSecs == 1:
			self.t[6] = int(self.t[6])
		
		elif nSecs < 86400:
			
			nFloor = self.t[4]*60*60 + self.t[5]*60 + int(self.t[6])
			
			nFloor = (nFloor / nSecs) * nSecs
			
			self.t[4] = nFloor / (60*60)
			nRem = nFloor - (self.t[4]*60*60)
			
			self.t[5] = nRem / 60
			self.t[6] = float( nRem - (self.t[5] * 60) )
			
		elif nSec == 86400:
			self.t[4] = 0
			self.t[5] = 0
			self.t[6] = 0.0
			
		else:
			raise ValueError("Can't yet provide floor values for times > 1 day")
		
		
		self.t = list( _das2.tnorm(self.t[0], self.t[1], self.t[2], 
		                           self.t[4], self.t[5], self.t[6]) )
		
	###########################################################################
	def ceil(self, nSecs):
		"""Find the nearest time, evenly divisible by nSec that is greater
		than the current time value."""
		
		if nSecs - int(nSecs) != 0:
			raise ValueError("%s is not an integer"%nSecs)		
		nSecs = int(nSecs)
		
		if nSecs < 1:
			raise ValueError("%s is < 1"%nSecs)
		
		elif nSecs == 1:
			rFrac = self.t[6] - int(self.t[6])
			if rFrac > 0.0:
				self.t[6] = int(self.t[6]) + 1
		
		elif nSecs < 86400:
			
			nSecOfDay = self.t[4]*60*60 + self.t[5]*60 + int(M.ceil(self.t[6]))
			
			nFloor = (nSecOfDay / nSecs) * nSecs
			
			if nSecOfDay - nFloor == 0:
				nCeil = nFloor
			else:
				nCeil = nFloor + nSecs
				
			self.t[4] = nCeil / (60*60)
			nRem = nCeil - (self.t[4]*60*60)
			
			self.t[5] = nRem / 60
			nRem = nRem - (self.t[5]*60)
			
			self.t[6] = float(nRem)
			
		elif nSecs == 86400:
			if (self.t[4] > 0) or (self.t[5] > 0) or (self.t[6] > 0.0):
				self.t[2] += 1
			
			self.t[4] = 0
			self.t[5] = 0
			self.t[6] = 0.0	
			
		else:
			raise ValueError("Can't yet provide floor values for times > 1 day")
			
			
		self.t = list( _das2.tnorm(self.t[0], self.t[1], self.t[2], 
		                           self.t[4], self.t[5], self.t[6]) )
		
	
	###########################################################################
		
	def year(self):
		"""Get the year value for the time"""
		return self.t[0]
	
	def month(self):
		"""Get the month of year, january = 1"""
		return self.t[1]
	
	def dom(self):
		"""Get the calendar day of month"""
		return self.t[2]
	
	def doy(self):
		"""Get the day of year, Jan. 1st = 1"""
		return self.t[3]
	
	def hour(self):
		"""Get the hour of day on a 24 hour clock (no am/pm)"""
		return self.t[4]
	
	def minute(self):
		"""Get the minute of the hour"""
		return self.t[5]
	
	def sec(self):
		"""Get floating point seconds of the minute"""
		return self.t[6]
		
	def set(self, **dArgs):
		"""Set one or more fields, call self.norm internally"""
		if dArgs.has_key('nYear'):
			self.t[0] = dArgs['nYear']
			
		if dArgs.has_key('nMonth'):
			self.t[1] = dArgs['nMonth']
		
		if dArgs.has_key('nDom'):
			self.t[2] = dArgs['nDom']
		
		if dArgs.has_key('nHour'):
			self.t[4] = dArgs['nHour']
			
		if dArgs.has_key('nMin'):
			self.t[5] = dArgs['nMin']
			
		if dArgs.has_key('fSec'):
			self.t[6] = dArgs['fSec']
			
		self.norm()
	
	# Converting to a datetime	
	def pyDateTime(self):
	
		nSec = int(self.t[6])
		nMicroSec = int( 1000000.0 * (self.t[6] - float(nSec) ) )
	
		pdt = datetime.datetime(self.t[0], self.t[1], self.t[2], 
		                       self.t[4], self.t[5], nSec, nMicroSec)
		return pdt
		
	# Rounding with field bump
	
	
	def domLeapIdx(self, nYear):
	
		if (nYear % 4) != 0:
			return 0
		
		if (nYear % 400) == 0:
			return 1
			
		if (nYear % 100) == 0:
			return 0
		else:
			return 1

	
	# Arguments for round
	YEAR = 1
	MONTH = 2
	DOM = 3
	DOY = 13
	HOUR = 4
	MINUTE = 5
	SEC = 6
	MILLISEC = 7
	MICROSEC = 8
	
	def _getTimePart(self, nWhich):
		
		if nWhich not in (DasTime.MILLISEC, DasTime.SEC, DasTime.MICROSEC):
			raise ValueError("Seconds precision unknown, expected one of"+\
			                 " DasTime.SEC, DasTime.MILLISEC, or DasTime.MICROSEC")
		
		nRnd = 0
		if nWhich >= DasTime.SEC:
			nFracDig = 0
			if nWhich >= DasTime.MILLISEC:
				nFracDig += 3
			if nWhich >= DasTime.MICROSEC:
				nFracDig += 3
			
			if nFracDig > 0:
				sFmt = "%%0%d.%df"%(nFracDig + 3, nFracDig)
			else:
				sFmt = "%02.0d"
			
			sSec = sFmt%self.sec()
			
			if sSec[0] == '6':
				sSec = '0'+sSec[1:]
				nRnd += 1
			
		nMin = self.minute() + nRnd
		nRnd = 0
		
		if nMin > 59:
			nMin -= 60
			nRnd += 1
		
		nHour = self.hour() + nRnd
		nRnd = 0
		
		if nHour > 23:
			nHour -= 24
			nRnd += 1
		
		return ("%02d:%02d:%s"%(nHour, nMin, sSec), nRnd)
		
	
	def round(self, nWhich):
		"""Round off times to Seconds, Milliseconds, or Microseconds
		nWhich - One of the constants: SEC, MILLISEC, MICROSEC
		returns as string to the desired precision in Year-Month-Day format
		"""
		
		(sTime, nRnd) = self._getTimePart(nWhich)
		
		nDom = self.dom() + nRnd
		nRnd = 0
		
		nYear = self.year()
		nMonth = self.month()
		
		nDaysInMonth = DasTime.dDaysInMon[self.domLeapIdx(nYear)][nMonth]
		
		if nDom > nDaysInMonth:
			nDom -= nDaysInMonth
			nMonth += 1
		
		if nMonth > 12:
			nMonth -= 12
			nYear += 1
				
		return "%04d-%02d-%02dT%s"%(nYear, nMonth, nDom, sTime)
	
	
	def round_doy(self, nWhich):
		"""Round off times to Seconds, Milliseconds, or Microseconds
		nWhich - One of the constants: SEC, MILLISEC, MICROSEC
		returns as string to the desired precision in Year-Day format
		"""
		
		(sTime, nRnd) = self._getTimePart(nWhich)
		
		nDoy = self.doy() + nRnd
		
		nYear = self.year()
		
		nDaysInYear = 365
		if self.domLeapIdx(nYear) == 1:
			nDaysInYear = 366
		
		if nDoy > nDaysInYear:
			nYear += 1
			nDoy -= nDaysInYear
						
		return "%04d-%03dT%s"%(nYear, nDoy, sTime)
	
	
	def isLeapYear(self):
		"""Returns true if the year field indicates a leap year on the 
		gregorian calendar, otherwise return false.
		"""
		raise ValueError("Not yet implemented")
	
		
	###########################################################################
	# Operator overloads
	
	# This date to interger calculation isn't good for anything except
	# differencing taken form Tyler Durden's algorithm on stackoverflow
	#
	#  http://stackoverflow.com/questions/12862226/the-implementation-of-calculating-the-number-of-days-between-2-dates
	#
	
	def _subHelper(self):
		
		y = self.t[0]
		m = self.t[1]
		d = self.t[2]
		
		m = (m + 9) % 12
		y = y - m/10
		return 365*y + y/4 - y/100 + y/400 + (m*306 + 5)/10 + ( d - 1 )
		
	
	def __sub__(self, other):
		"""WARNING: This function works very differently depending on the
		type of the other object.  If the other item is a DasTime, then
		the difference in floating point seconds is returned.  
		
		If the other type is a simple numeric type than a new DasTime 
		is returned which is smaller than the initial one by 'other' seconds.
		
		Time subtractions between two DasTime objects are handled in a way
		that is sensitive to small differences.  Diferences a small as the 
		smalleset possible positive floating point value times 60 should be 
		preserved. 
		
		Time difference in seconds is returned.  This method should be valid
		as long as you are using the gegorian calendar, but *doesn't* account
		for leap seconds.  Leap second handling could be added via a table
		if needed.
		"""
		
		if isinstance(other, DasTime):
		
			fDiff = (self.t[4]*3600 + self.t[5]*60 + self.t[6])  - \
			        (other.t[4]*3600 + other.t[5]*60 + other.t[6])
		
			# Now convert days since 1970 as a unit, fastest way is to convert
			# to a julian date and subtract those.
			nDiff = self._subHelper() - other._subHelper()
			fDiff += nDiff * 86400.0
	
			return fDiff
		
		else:
			t = [self.t[0], self.t[1], self.t[2], self.t[4], self.t[5], 
		     self.t[6] - other]
		
			return DasTime( t[0], t[1], t[2], t[3], t[4], t[5] )
			
	
	
	def __isub__(self, other):
		
		t = [self.t[0], self.t[1], self.t[2], self.t[4], self.t[5], 
		     self.t[6] - other]
			  
		self.t = list( _das2.tnorm(t[0], t[1], t[2], t[3], t[4], t[5]) )
		
		return self

		
	def __add__(self, other):
		"""Add a floating point time in seconds to the current time point"""
		
		t = [self.t[0], self.t[1], self.t[2], self.t[4], self.t[5], 
		     self.t[6] + other]
		
		return DasTime( t[0], t[1], t[2], t[3], t[4], t[5] )
	
	def __radd__(self, other):
		return self.__add__(other)
		
	
	def __iadd__(self, other):
		
		t = [self.t[0], self.t[1], self.t[2], self.t[4], self.t[5], 
		     self.t[6] + other]
			  
		self.t = list( _das2.tnorm(t[0], t[1], t[2], t[3], t[4], t[5]) )
		
		return self
		
		
###############################################################################

import unittest
	
	
	
	
	
	
	
	
