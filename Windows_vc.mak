# A GNU Makefile.  To get gnu make for windows goto:
# 
#   http://gnuwin32.sourceforge.net/  
#
# and get the 'make' package.  
#
# This makefile expects a working uname command, to get this goto the
# site above and get the 'CoreUtils' package.
#
# makefile for libdas.a on WindowsXP using windows native command line tools
# you can get these by downloading "Visual Studio 2008 Express"
# To get the tools on to you path run the included vcvars32.bat file before
# attempting to run this program.
#
# DO NOT RUN under Cygwin!!!  This expects a native windows compliler
# as well as the CMD.EXE shell.

##############################################################################
# Generic Definitions

CC=cl.exe
SHELL=cmd.exe

# Pick a default install location, if user doesn't have one defined
ifeq ($(PREFIX),)
PREFIX=$(USERPROFILE)
endif

INST_LIB=$(PREFIX)\lib
INST_INC=$(PREFIX)\include

##############################################################################
# GNU really has problems with filenames with spaces, probably a good
# reason to choose a different tool.

sp :=
sp +=
qs = $(subst ?,$(sp),$1)
sq = $(subst $(sp),?,$1)

##############################################################################
# Specific Definitions

CFLAGS=/Zi /Wall /nologo /D_CRT_SECURE_NO_WARNINGS

SRCS=parsetime.c ttime.c daspkt.c
HDRS=das.h

OBJS= $(patsubst %.c, $(BUILD_DIR)/%.obj, $(SRCS))

##############################################################################
# Pattern rules

.SUFFIXES:

$(BUILD_DIR)/%.obj:src/%.c |  $(BUILD_DIR)
	$(CC) $(CFLAGS) /c $< /Zi /Fo$@ /Fd$@.pdb

##############################################################################
# Explicit rules

all: $(BUILD_DIR) $(BUILD_DIR)/das.lib $(BUILD_DIR)/prtime.exe \
  $(BUILD_DIR)/inctime.exe $(BUILD_DIR)/das_pdslist.exe


$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	
$(BUILD_DIR)/das.lib: $(OBJS)
	lib.exe /nologo /verbose /out:$@ $(OBJS)
	
$(BUILD_DIR)/prtime.exe:$(BUILD_DIR)/prtime.obj $(BUILD_DIR)/das.lib
	$(CC) $(CFLAGS) $^ /Zi /Fe$@ 
	
$(BUILD_DIR)/inctime.exe:$(BUILD_DIR)/inctime.obj $(BUILD_DIR)/das.lib
	$(CC) $(CFLAGS) $^ /Zi /Fe$@ 

$(BUILD_DIR)/das_pdslist.exe:$(BUILD_DIR)/das_pdslist.obj $(BUILD_DIR)/das.lib
	$(CC) $(CFLAGS) $^ /Zi /Fe$@ 

	
#install: "$(BUILD_DIR)/das.lib" "$(INST_INC)/das.h" "$(INST_LIB)/das.lib"
install:
	@if not exist "$(INST_NAT_LIB)" mkdir "$(INST_NAT_LIB)"
	copy /Y $(BUILD_DIR)\das.lib "$(INST_NAT_LIB)\das.lib"
	@if not exist "$(INST_INC)" mkdir "$(INST_INC)"
	copy /Y src\das.h "$(INST_INC)\das.h"
	@if not exist "$(INST_NAT_BIN)" mkdir "$(INST_NAT_BIN)"
	copy /Y $(BUILD_DIR)\prtime.exe "$(INST_NAT_BIN)\prtime.exe"
	copy /Y $(BUILD_DIR)\inctime.exe "$(INST_NAT_BIN)\inctime.exe"
	copy /Y $(BUILD_DIR)\das_pdslist.exe "$(INST_NAT_BIN)\das_pdslist.exe"


$(BUILD_DIR)/_daslib.so:
	python setup.py build_ext -b $(BUILD_DIR) -t $(BUILD_DIR)
	
pylib:$(BUILD_DIR)/_daslib.so

clean:
	del $(OBJS)
	
distclean:
	rmdir /S /Q $(BUILD_DIR)
