##############################################################################
# A native Windows GNU Makefile.
#
# The preferred way to get gnu make for windows is to download as part of a
# mingw distribution.  You can get mingw in many places, if you are not doing
# graphical development you can grab it from:
#
# http://tdm-gcc.tdragon.net/
#
# A better source is the Qt GUI library with bundled python and mingw compiler
# at:
#
# http://qt-project.org/downloads
#
# Always use mingw32-make (or equivalent) never simple make.exe that is supplied
# with MSYS among other places.  This is a NATIVE makefile that uses cmd.exe
# for the shell, don't run it under Cygwin!

##############################################################################
# Generic Definitions

SHELL=cmd.exe

##############################################################################

CC=gcc

#CFLAGS= -O -Wall -I. -std=c99
CFLAGS= -g -Wall -I. -std=c99

SRCS=parsetime.c ttime.c daspkt.c das_cli.c
HDRS=das.h das_cli.h

OBJS= $(patsubst %.c, $(BUILD_DIR)/%.obj, $(SRCS))

##############################################################################
# Pattern rules

.SUFFIXES:

$(BUILD_DIR)/%.obj:src/%.c |  $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -o $@ $<
	
# Making test programs
$(BUILD_DIR)/%:test/%.c $(BUILD_DIR)/libdas.a
	$(CC) $(CFLAGS) -I ./src -o $@ $< $(BUILD_DIR)/libdas.a -lm

$(INST_INC)/%.h:src/%.h
	install $< $@

$(INST_NAT_BIN)/%:$(BUILD_DIR)/%
	install $< $@

##############################################################################
# Explicit rules

.PHONY : all install pylib pylib_install

all: $(BUILD_DIR) $(BUILD_DIR)/libdas.a $(BUILD_DIR)/prtime.exe \
  $(BUILD_DIR)/inctime.exe $(BUILD_DIR)/das_pdslist.exe
	
test: $(BUILD_DIR)/libdas.a $(BUILD_DIR)/ex_das_cli $(BUILD_DIR)/ex_das_ephem
	

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

$(BUILD_DIR)/libdas.a: $(OBJS)
	ar rv $@ $(OBJS)
	
$(BUILD_DIR)/prtime.exe:$(BUILD_DIR)/prtime.obj $(BUILD_DIR)/libdas.a
	$(CC) $(CFLAGS) $^ -lm -o $@ 
	
$(BUILD_DIR)/inctime.exe:$(BUILD_DIR)/inctime.obj $(BUILD_DIR)/libdas.a
	$(CC) $(CFLAGS) $^ -lm -o $@
	
$(BUILD_DIR)/das_pdslist.exe:$(BUILD_DIR)/das_pdslist.obj $(BUILD_DIR)/libdas.a
	$(CC) $(CLFAS) $^ -lm -o $@

install: $(INST_NAT_LIB)/libdas.a $(INST_INC)/das.h $(INST_INC)/das_cli.h \
  $(INST_NAT_BIN)/prtime.exe $(INST_NAT_BIN)/inctime.exe $(INST_NAT_BIN)/das_pdslist.exe
  
$(INST_NAT_LIB)/libdas.a:$(BUILD_DIR)/libdas.a
	install $< $@
	

$(BUILD_DIR)/_daslib.pyd:
	python setup.py build_ext --compiler=mingw32 -b $(BUILD_DIR) -t $(BUILD_DIR)

pylib:$(BUILD_DIR)/_daslib.so

pylib_install:$(BUILD_DIR)/_daslib.pyd
	install $(BUILD_DIR)/_daslib.pyd $(INST_EXT_LIB)/daslib/_daslib.pyd
	install src/__init__.py $(INST_EXT_LIB)/daslib/__init__.py
	install src/daspkt.py $(INST_EXT_LIB)/daslib/daspkt.py
	install src/dastime.py $(INST_EXT_LIB)/daslib/dastime.py
	install src/dascli.py $(INST_EXT_LIB)/daslib/dascli.py
	python -c "import compileall; compileall.compile_dir('$(INST_EXT_LIB)/daslib', force=True)"

distclean:
	rmdir /S /Q $(BUILD_DIR)
