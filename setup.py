#!/usr/bin/env python

import os
import sys
import os.path
from distutils.core import setup, Extension

lDefs = []
lLibDirs = []
if sys.platform == 'win32':
	lDefs = [("_CRT_SECURE_NO_WARNINGS", None)]
	lLibDirs = [ os.path.dirname(sys.executable) ]

lInc = ['.']
lSrc = ["das2/d1_daspkt.c", "das2/d1_ttime.c", "das2/d1_parsetime.c", "das2/pywrap.c", "das2/dft.c", "das2/util.c"]

if sys.platform.lower().startswith('sunos'):
	ext = Extension(
		"_das2", sources=lSrc, include_dirs=lInc, define_macros=lDefs
		,library_dirs=lLibDirs, libraries=["fftw3"]
		,extra_compile_args=["-xc99"]
	)
else:
	ext = Extension(
		"_das2", sources=lSrc, include_dirs=lInc, define_macros=lDefs
		,library_dirs=lLibDirs, libraries=["fftw3"]
		,extra_compile_args=['-std=c99']
	)


setup(
	name="das2",
	version="0.1",
	ext_modules=[ ext ],
	description="ASCII time string parsing plus Das interface helpers",
	author="Larry Granroth, Jeremy Faden, Chris Piker",
	author_email="larry-granroth@uiowa.edu",
	url="https://saturn.physics.uiowa.edu/svn/das2/core/stable/libdas2"
)

