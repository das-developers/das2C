# Use GNU make 
ifneq ($(MAKE),gmake)
$(error This make file is intended for use with gmake)
endif

# Project definitions

TARG=libdas2.a

SRCS=d1_parsetime.c d1_ttime.c d1_daspkt.c util.c buffer.c utf8.c units.c \
 encoding.c descriptor.c das2io.c oob.c processor.c stream.c packet.c plane.c \
 dsdf.c dft.c log.c
   
# These headers are always installed
HDRS=utf8.h util.h buffer.h units.h encoding.h descriptor.h plane.h packet.h \
 stream.h oob.h das2io.h processor.h dsdf.h dft.h log.h das1.h core.h das.h \
 log.h

# Utility programs to always install
UTIL_PROGS=das1_inctime das2_prtime das1_fxtime
  
# Example and test single file programs
EX_PROGS=inputExample outputYScanExample outputYScanMultiPacketExample \
 sineWaveExample taskProgressExample

TEST_PROGS=TestUnits

BD=$(BUILD_DIR)

##############################################################################
# Build definitions

# Add large file support for Linux NFS compatibility
LFS_CFLAGS=$(shell getconf LFS_CFLAGS) 

CC=cc
#-D_XOPEN_SOURCE=600

CFLAGS=-xc99 -errwarn -I. -I$(INST_INC) -I/local/include -g $(LFS_CFLAGS)
LFLAGS=-L$(INST_NAT_LIB) /local/lib/libfftw3.a /local/lib/libexpat.a -lz -lm


##############################################################################
# Derived definitions

BUILD_OBJS= $(patsubst %.c,$(BD)/%.o,$(SRCS))

UTIL_OBJS= $(patsubst %,$(BD)/%.o,$(UTIL_PROGS))

INST_HDRS= $(patsubst %.h,$(INST_INC)/das2/%.h,$(HDRS))

BUILD_UTIL_PROGS= $(patsubst %,$(BD)/%, $(UTIL_PROGS))

INST_UTIL_PROGS= $(patsubst %,$(INST_NAT_BIN)/%, $(UTIL_PROGS))

BUILD_EX_PROGS= $(patsubst %,$(BD)/%, $(EX_PROGS))

BUILD_TEST_PROGS = $(patsubst %,$(BD)/%, $(TEST_PROGS))

##############################################################################
# Pattern Rules

# Pattern rule for generating dependency makefiles
$(BD)/%.d:%.c | $(BD)
	@echo "Generating $@"
	set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.tmp; \
	sed 's|$*.o|$(BD)/& $@|g' $@.tmp > $@; \
	rm -f $@.tmp

# Pattern rules building C source lib code to object files
$(BD)/%.o:das2/%.c | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BD)/%.o:utilities/%.c | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 
	
	
# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 

$(BD)/%:example/%.c | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 

# Pattern rule for installing static libraries
$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -D -m 664 $< $@	 

# Pattern rule for installing library header files
$(INST_INC)/das2/%.h:das2/%.h
	install -D -m 664 $< $@	 
	
# Pattern rule for installing binaries
$(INST_NAT_BIN)/%:$(BD)/%
	install -D -m 775 $< $@	 
	

# Explicit Rules #############################################################

# Direct make not to nuke the intermediate .o files
.SECONDARY: $(BUILD_OBJS)  $(UTIL_OBJS)
.PHONY: test

# Primary target
build:$(BD) $(BD)/$(TARG) $(BUILD_UTIL_PROGS)

$(BD):
	@if [ ! -d "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi
  
$(BD)/$(TARG):$(BUILD_OBJS)
	ar rc $@ $(BUILD_OBJS)

pylib:$(BD)/_das2.so das2/*.py
	cp das2/__init__.py $(BD)
	cp das2/dastime.py $(BD)

$(BD)/_das2.so: das2/pywrap.c das2/das1.h das2/d1_*.c
	python$(PYVER) setup.py build_ext -b $(BD) -t $(BD)

examples: $(BD) $(BD)/$(TARG) $(BUILD_EX_PROGS)

## Explicit Rules, Testing ###################################################

test: $(BD) $(BD)/$(TARG) $(BUILD_TEST_PROGS)
	test/test_dastime.py 
	test/das1_fxtime_test.sh $(BD)
	test/das2_dastime_test1.sh $(BD)	


## Explicit Rules, Documentation #############################################	

html:
	doxygen

install_html:
	-mkdir -p $(INST_DOC)
	-rm -r $(INST_DOC)/das2Stream
	cp -r html $(INST_DOC)/das2Stream

## Explicit Rules, Installation ##############################################

# Install C-lib, Python Lib and Utility programs
install:$(INST_NAT_LIB)/$(TARG) $(INST_HDRS) $(INST_UTIL_PROGS)

install_pylib:$(BD)/_das2.so
	install -D -m 775 $(BD)/_das2.so $(INST_EXT_LIB)/_das2.so
	install -D -m 664 $(BD)/__init__.py $(INST_HOST_LIB)/das2/__init__.py
	install -D -m 664 $(BD)/dastime.py $(INST_HOST_LIB)/das2/dastime.py
	python$(PYVER) -c "import compileall; compileall.compile_dir('$(INST_HOST_LIB)/das2', force=True)"


lib_install:$(INST_NAT_LIB)/$(TARG) $(INST_HDRS)



# Auto make and grab the dependencies
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
-include $(OBJS:.o=.d)
endif
endif

distclean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi
	if [ -d doc/html ]; then rm -r doc/html; fi

clean:
	rm $(BD)/*.o $(BD)/*.d



