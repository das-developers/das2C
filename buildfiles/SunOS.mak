# Use GNU make 
ifneq ($(MAKE),gmake)
$(error This make file is intended for use with gmake)
endif


# Export name of md5sum command (needed to make sub-scipts work on MacOS)

MD5SUM:=$(shell which md5sum)
export MD5SUM


##############################################################################
# Project definitions

TARG=libdas3.0

SRCS=time.c das1.c util.c log.c buffer.c utf8.c value.c tt2000.c units.c  \
 operator.c datum.c array.c encoding.c variable.c descriptor.c dimension.c \
 dataset.c plane.c packet.c stream.c processor.c oob.c io.c builder.c dsdf.c \
 credentials.c http.c dft.c json.c node.c
 
HDRS=defs.h time.h das1.h util.h log.h buffer.h utf8.h value.h units.h \
 tt2000.h operator.h datum.h array.h encoding.h variable.h descriptor.h \
 dimension.h dataset.h plane.h packet.h stream.h processor.h oob.h io.h \
 builder.h dsdf.h credentials.h http.h dft.h json.h node.h core.h
 
UTIL_PROGS=das1_inctime das2_prtime das1_fxtime das2_ascii das2_bin_avg \
 das2_bin_avgsec das2_bin_peakavgsec das2_from_das1 das2_from_tagged_das1 \
 das1_ascii das1_bin_avg das2_bin_ratesec das2_psd das2_hapi das2_histo \
 das2_cache_rdr

TEST_PROGS=TestUnits TestArray TestVariable LoadStream TestBuilder \
 TestAuth TestCatalog TestTT2000 

BD=$(BUILD_DIR)

##############################################################################
# Build definitions

DEFINES=-DWISDOM_FILE=$INST_ETC/fftw/wisdom

# Add large file support for Linux NFS compatibility
LFS_CFLAGS=$(shell getconf LFS_CFLAGS) 

CC=cc -D_XOPEN_SOURCE=600

CSW_INC:=/opt/csw/include
CFLAGS:=-xc99 -xcode=pic32 -I. -I$(INST_INC) -I$(CSW_INC) $(DEFINES) -g $(CFLAGS)
CTESTFLAGS=$(CFLAGS)

CSW_LIB:=/opt/csw/lib
LFLAGS:=-L$(CSW_LIB) -L$(INST_NAT_LIB) -lfftw3 -lexpat -lssl -lcrypto -lsocket -lrt -lz -lm -lpthread



##############################################################################
# Derived definitions

BUILD_OBJS= $(patsubst %.c,$(BD)/%.o,$(SRCS))

UTIL_OBJS= $(patsubst %,$(BD)/%.o,$(UTIL_PROGS))

INST_HDRS= $(patsubst %.h,$(DESTDIR)$(INST_INC)/das2/%.h,$(HDRS))

BUILD_UTIL_PROGS= $(patsubst %,$(BD)/%, $(UTIL_PROGS))

INST_UTIL_PROGS= $(patsubst %,$(DESTDIR)$(INST_NAT_BIN)/%, $(UTIL_PROGS))

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

# Pattern rule for building C source files from flex files
src/%.c:src/%.l
	$(LEX) -o $@ $<

# Pattern rule for building C source files from bison files
src/%.c:src/%.y
	$(YACC) -o $@ $<
	
# Pattern rule for building object files from C source files
$(BD)/%.o:das2/%.c | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BD)/%.o:utilities/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@ 
		
# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) $(CTESTFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@ 

# Pattern rule for installing static libraries
$(DESTDIR)$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -D -m 664 $< $@
	
# Pattern rule for installing dynamic libraries
$(DESTDIR)$(INST_NAT_LIB)/%.so:$(BD)/%.so
	 install -D -m 775 $< $@	

# Pattern rule for installing library header files
$(DESTDIR)$(INST_INC)/das2/%.h:das2/%.h
	install -D -m 664 $< $@	 

# Pattern rule for installing binaries
$(DESTDIR)$(INST_NAT_BIN)/%:$(BD)/%
	install -D -m 775 $< $@	 
	

# Direct make not to nuke the intermediate .o files
.SECONDARY: $(BUILD_OBJS) $(UTIL_OBJS)
.PHONY: test

## Explicit Rules  ###########################################################

build:$(BD) $(BD)/$(TARG).a $(BD)/$(TARG).so \
 $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS)

$(BD)/$(TARG).a:$(BUILD_OBJS)
	ar rc $@ $(BUILD_OBJS)
	
$(BD)/$(TARG).so:$(BUILD_OBJS)
	$(CC) -G -o $@ $(BUILD_OBJS)

$(BD):
	@if [ ! -d "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi
	   
	
# Robert's tagged das1 reader breaks strict-aliasing expectations for C99
# and thus should not be optimized least it provide incorrect output. 
# An explicit override to build a debug version instead is provided here.
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
# override pattern rule for das2_bin_ratesec.c, it hase two object files
$(BD)/das2_bin_ratesec:$(BD)/das2_bin_ratesec.o $(BD)/via.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@ 

# override pattern rule for das2_psd, has extra libraries
$(BD)/das2_psd:$(BD)/das2_psd.o $(BD)/send.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@ 
	

# Run tests
test: $(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	env DIFFCMD=gdiff test/das1_fxtime_test.sh $(BD)
	test/das2_ascii_test1.sh $(BD)
	test/das2_ascii_test2.sh $(BD)	
	test/das2_bin_avgsec_test1.sh $(BD)
	test/das2_bin_avgsec_test2.sh $(BD)
	test/das2_bin_peakavgsec_test1.sh $(BD)
	test/das2_from_das1_test1.sh $(BD)
	test/das2_from_das1_test2.sh $(BD)
	test/das2_histo_test1.sh $(BD)
	@echo "INFO: Running unit test to test units, $(BD)/TestUnits..." 
	@$(BD)/TestUnits
	@echo "INFO: Running unit test for TT2000 leap seconds, $(BD)/TestTT2000..." 
	@$(BD)/TestTT2000
	@echo "INFO: Running unit test for dynamic arrays, $(BD)/TestArray..."
	@$(BD)/TestArray
	@echo "INFO: Running unit test for index space mapping, $(BD)/TestVariable..."
	@$(BD)/TestVariable
	@echo "INFO: Running unit test for catalog reader, $(BD)/TestCatalog..."
	@$(BD)/TestCatalog
	@echo "INFO: Running unit test for dataset builder, $(BD)/TestBuilder..."
	@$(BD)/TestBuilder
	@echo "INFO: All test programs completed without errors"


# Install everything
install:lib_install $(INST_UTIL_PROGS)

lib_install:$(DESTDIR)$(INST_NAT_LIB)/$(TARG).a $(DESTDIR)$(INST_NAT_LIB)/$(TARG).so $(INST_HDRS)

# Does not install static object that that it can be used with proprietary
# software
so_install:$(INST_NAT_LIB)/$(TARG).so $(INST_HDRS)

	
# Documentation ##############################################################
doc:$(BD)/html

$(BD)/html:$(BD) $(BD)/$(TARG).a
	@cd $(BD)
	(cat Doxyfile; echo "HTML_OUTPUT = $@ ") | doxygen -

install_doc:$(INST_DOC)/libdas2

$(INST_DOC)/libdas2:$(BD)/html
	-mkdir -p $(INST_DOC)
	@if [ -e "$(INST_DOC)/libdas2" ]; then rm -r $(INST_DOC)/libdas2; fi
	cp -r $(BD)/html $(INST_DOC)/libdas2


# Cleanup ####################################################################
distclean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi

clean:
	rm -r $(BD)


## Automatic dependency tree generation for C code ###########################

#ifneq ($(MAKECMDGOALS),clean)
#ifneq ($(MAKECMDGOALS),distclean)
#-include $(OBJS:.o=.d)
#endif
#endif




