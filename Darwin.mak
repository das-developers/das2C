# Overrides for MacOS
N_ARCH=Darwin

# Project definitions

TARG=libdas2.a
TARG_D=libdas2_d.a

SRCS=d1_parsetime.c d1_ttime.c d1_daspkt.c util.c buffer.c utf8.c units.c \
 encoding.c descriptor.c das2io.c oob.c processor.c stream.c packet.c plane.c \
 dsdf.c dft.c data.c log.c
   
 #StreamToolsCreate.c StreamToolsRead.c
 
DSRCS=package.d daspkt.d dwrap.d

# These headers are always installed
HDRS=utf8.h util.h buffer.h units.h encoding.h descriptor.h plane.h packet.h \
 stream.h oob.h das2io.h processor.h dsdf.h dft.h das1.h core.h das.h \
 log.h data.h
 
D_HDRS=package.d dwrap.d daspkt.d

# Utility programs to always install
UTIL_PROGS=das1_inctime das2_prtime das1_fxtime
  
# Example and test single file programs
EX_PROGS=inputExample outputYScanExample outputYScanMultiPacketExample \
 sineWaveExample taskProgressExample

TEST_PROGS=TestUnits

BD=$(BUILD_DIR)

##############################################################################
# Build definitions

CC=gcc

#CFLAGS=-Wall -fPIC -std=c99 -I. -I$(INST_INC) -ggdb
CFLAGS=-Wall -O3 -fPIC -std=c99 -I. -I$(INST_INC)
CTESTFLAGS=-Wall -fPIC -std=c99 -I. -ggdb

LFLAGS=-L$(INST_NAT_LIB) -lexpat -lz -lm
LTESTFLAGS= -lexpat -lz -lm

DC=dmd
DFLAGS=-g -od$(PWD)/$(BD) -w -m64

##############################################################################
# Derived definitions


D_SRCS=$(patsubst %.d,das2/%.d,$(DSRCS))

BUILD_OBJS= $(patsubst %.c,$(BD)/%.o,$(SRCS))

UTIL_OBJS= $(patsubst %,$(BD)/%.o,$(UTIL_PROGS))

INST_HDRS= $(patsubst %.h,$(INST_INC)/das2/%.h,$(HDRS))
INST_D_HDRS = $(patsubst %.d,$(INST_INC)/D/das2/%.d,$(D_HDRS))

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

$(BD)/%.o:utilities/%.c $(BD)/$(TARG) | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 
		
# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c $(BD)/$(TARG) | $(BD)
	$(CC) $(CTESTFLAGS) $< $(BD)/$(TARG) $(LTESTFLAGS) -o $@ 

$(BD)/%:example/%.c $(BD)/$(TARG) | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LTESTFLAGS) -o $@ 

# Pattern rule for installing static libraries
$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -D -m 664 $< $@	 

# Pattern rule for installing library header files
$(INST_INC)/das2/%.h:das2/%.h
	install -D -m 664 $< $@	 
	
# Pattern rule for installing d-library header files
$(INST_INC)/D/das2/%.d:das2/%.d
	install -D -m 664 $< $@	 

# Pattern rule for installing binaries
$(INST_NAT_BIN)/%:$(BD)/%
	install -D -m 775 $< $@	 
	

# Direct make not to nuke the intermediate .o files
.SECONDARY: $(BUILD_OBJS) $(UTIL_OBJS)
.PHONY: test


## Explicit Rules, Building ##################################################

build:$(BD) $(BD)/$(TARG) $(BUILD_UTIL_PROGS) $(BD)/_das2.so das2/*.py
	cp das2/__init__.py $(BD)
	cp das2/dastime.py $(BD)
	cp das2/toml.py $(BD)

dlib:$(BD)/$(TARG_D)

prn_vars:
	@echo PREFIX='"$(PREFIX)"'
	@echo INST_ETC='$(INST_ETC)'
	@echo INST_INC='$(INST_INC)'
	@echo INST_LIB='$(INST_LIB)'
	@echo INST_HOST_LIB='$(INST_HOST_LIB)'
	@echo INST_EXT_LIB='$(INST_EXT_LIB)'
	@echo INST_NAT_BIN='$(INST_NAT_BIN)'

examples: $(BD) $(BD)/$(TARG) $(BUILD_EX_PROGS)


$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi

$(BD)/$(TARG):$(BUILD_OBJS)
	ar rc $@ $(BUILD_OBJS)

$(BD)/$(TARG_D):$(D_SRCS) $(BD)/$(TARG)
	$(DC) $^ -lib $(DFLAGS) -of$(notdir $@)


$(BD)/_das2.so: das2/pywrap.c das2/das1.h das2/d1_*.c
	python$(PYVER) setup.py build_ext -b $(BD) -t $(BD)
	

## Explicit Rules, Testing ###################################################

# Run tests
test: $(BD) $(BD)/$(TARG) $(BUILD_TEST_PROGS)
	test/test_dastime.py 
	test/das1_fxtime_test.sh $(BD)
	test/das2_dastime_test1.sh $(BD)


## Explicit Rules, Installation ##############################################

# Install C-lib, Python Lib and Utility programs
install:$(INST_NAT_LIB)/$(TARG) $(INST_HDRS) $(BD)/_das2.so $(INST_UTIL_PROGS)
	install -D -m 775 $(BD)/_das2.so $(INST_EXT_LIB)/_das2.so
	install -D -m 664 $(BD)/__init__.py $(INST_HOST_LIB)/das2/__init__.py
	install -D -m 664 $(BD)/dastime.py $(INST_HOST_LIB)/das2/dastime.py
	install -D -m 664 $(BD)/toml.py $(INST_HOST_LIB)/das2/toml.py
	python$(PYVER) -c "import compileall; compileall.compile_dir('$(INST_HOST_LIB)/das2', force=True)"
	
dlib_install:$(INST_NAT_LIB)/$(TARG_D) $(INST_D_HDRS)


## Explicit Rules, Documentation #############################################

html:
	doxygen

install_html:
	-mkdir -p $(INST_DOC)
	-rm -r $(INST_DOC)/das2stream
	cp -r html $(INST_DOC)/das2stream



## Automatic dependency tree generation for C code ###########################

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
-include $(OBJS:.o=.d)
endif
endif

distclean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi
	if [ -d doc/html ]; then rm -r doc/html; fi

clean:
	rm -r $(BD)



