# Export name of md5sum command (needed to make sub-scipts work on MacOS)

MD5SUM=md5sum
export MD5SUM

BD=$(BUILD_DIR)

##############################################################################
# Upstream Source dependencies

# You may already have libcdf.a and cspice.a in your build tree for other
# parts of your project.  If so use them by setting .  If not, you can issue build depend first and dependency
# sources will be downloaded and built.

LOC_CDF_DIST:=cdf39_0-dist
LOC_CDF_URL:=https://spdf.gsfc.nasa.gov/pub/software/cdf/dist/cdf39_0/linux/$(LOC_CDF_DIST)-cdf.tar.gz
LOC_CSPICE_URL:=https://naif.jpl.nasa.gov/pub/naif/toolkit//C/PC_Linux_GCC_64bit/packages/cspice.tar.Z

ifeq ($(BLD_CDF),1)
CDF_INC:=$(BD)/$(LOC_CDF_DIST)/src/include
CDF_LIB:=$(BD)/$(LOC_CDF_DIST)/src/lib/libcdf.a
CDF_SO:=$(BD)/$(LOC_CDF_DIST)/src/lib/libcdf.so
CDF_SO_REDIST:=$(BD)/libcdf.so
endif

ifeq ($(BLD_CSPICE),1)
CSPICE_INC:=./$(BD)/cspice/include
CSPICE_LIB:=./$(BD)/cspice/lib/cspice.a
endif


##############################################################################
# Project definitions

TARG=libdas3

SRCS:=das1.c array.c buffer.c builder.c cli.c codec.c credentials.c dataset.c \
dataset_hdr2.c dataset_hdr3.c datum.c descriptor.c dft.c dimension.c dsdf.c \
encoding.c frame.c http.c io.c iterator.c json.c log.c node.c oob.c operator.c \
packet.c plane.c processor.c property.c send.c stream.c time.c tt2000.c \
units.c utf8.c util.c value.c var_base.c var_con.c var_seq.c var_ary.c var_una.c \
var_bin.c vector.c 
 
HDRS:=defs.h time.h das1.h util.h log.h buffer.h utf8.h value.h units.h \
 tt2000.h operator.h datum.h frame.h array.h encoding.h variable.h descriptor.h \
 dimension.h dataset.h plane.h packet.h stream.h processor.h property.h oob.h \
 io.h iterator.h builder.h dsdf.h credentials.h http.h dft.h json.h node.h cli.h \
 send.h vector.h codec.h core.h 
 
ifeq ($(SPICE),yes)
SRCS:=$(SRCS) spice.c
HDRS:=$(HDRS) spice.h
endif
 
UTIL_PROGS=das1_inctime das2_prtime das1_fxtime das2_ascii das2_bin_avg \
 das2_bin_avgsec das2_bin_peakavgsec das2_from_das1 das2_from_tagged_das1 \
 das1_ascii das1_bin_avg das2_bin_ratesec das2_psd das2_hapi das2_histo \
 das2_cache_rdr das3_node das3_csv das3_test

TEST_PROGS:=TestUnits TestArray TestVariable TestBuilder \
 TestAuth TestCatalog TestTT2000 ex_das_cli ex_das_ephem TestCredMngr \
 TestV3Read TestIter

CDF_PROGS:=das3_cdf
 
ifeq ($(SPICE),yes)
UTIL_PROGS:=$(UTIL_PROGS) das3_spice
TEST_PROGS:=$(TEST_PROGS) TestSpice
endif

ifeq ($(CDF),yes)
UTIL_PROGS:=$(UTIL_PROGS) das3_cdf
endif

##############################################################################
# Build definitions

LEX=flex
YACC=bison

# -Wno-deprecated needed for OpenSSL, removed when call to ERR_load_BIO_strings is fixed

# -Wno-stringop-truncation needed because I use string trucation all the time
#    intentianally and make sure it's null terminated anyway, so this error
#    doesn't apply

WARNINGS:=-Wall -Werror -Wno-deprecated-declarations -Wno-stringop-truncation

#-Wno-format-security


DEFINES:=-DWISDOM_FILE=/etc/fftw/wisdom -D_XOPEN_SOURCE=600 -Wno-format-truncation 

DBG_DEFINES=-ggdb 
O2_DEFINES=-DNDEBUG -O2 -Wno-unused-result -Wno-stringop-truncation 


# Conda build does NOT set the include and lib directories within the
# compiler script itself, it merely exports ENV vars. This is unfortunate
# because it means makefiles must be altered to work with anaconda.

ifeq ($(CONDA_BUILD_STATE),)

CC=gcc 

CFLAGS= $(WARNINGS) $(DBG_DEFINES) $(DEFINES) -fPIC -std=c99  -I.
#CFLAGS=$(WARNINGS) $(O2_DEFINES) $(DEFINES) -fPIC -std=c99 -I.

#-fstack-protector-strong 

#CTESTFLAGS=-Wall -fPIC -std=c99 -ggdb -I. $(CFLAGS)
CTESTFLAGS=$(CFLAGS)

LFLAGS= -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread

else

CFLAGS:=-std=c99 $(WARNINGS) -I. -Wno-format-security $(DEFINES) $(CFLAGS)
CTESTFLAGS=-std=c99 -I. $(CFLAGS)

LFLAGS:=$(LDFLAGS) -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread

endif

ifeq ($(SPICE),yes)
LFLAGS:=$(CSPICE_LIB) $(LFLAGS)
CFLAGS:=$(CFLAGS) -I$(CSPICE_INC)
endif


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
.PHONY: test build

## Explicit Rules  ###########################################################

# Building dependencies if needed
#ifeq ($(BLD_CSPICE)$(BLD_CDF),00)

#build: $(BD)/$(TARG).a $(BD)/$(TARG).so $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
#build_static: $(BD) $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)

#else

build: build_dep $(BD)/$(TARG).a $(BD)/$(TARG).so $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
build_static:build_dep $(BD) $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)

#endif

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi

$(BD)/$(TARG).a:$(BUILD_OBJS) | $(BD)
	ar rc $@ $(BUILD_OBJS)
	
$(BD)/$(TARG).so:$(BUILD_OBJS) | $(BD)
	gcc -shared -o $@ $(BUILD_OBJS)
	   
	
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
	
cdf:$(BD)/das3_cdf

$(BD)/das3_cdf:utilities/das3_cdf.c $(BD)/$(TARG).a
	@echo "An example CDF_INC value would be: /usr/local/include"
	@echo "An example CDF_LIB value would be: /usr/local/lib/libcdf.a"
	@if [ "$(CDF_INC)" = "" ] ; then echo "CDF_INC not set"; exit 3; fi
	@if [ "$(CDF_LIB)" = "" ] ; then echo "CDF_LIB not set"; exit 3; fi
	$(CC) $(CFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)


# Conditional rule
ifeq ($(BLD_CSPICE)$(BLD_CDF),11)
build_dep:$(CSPICE_LIB) $(CDF_LIB) $(CDF_SO_REDIST)
else ifeq ($(BLD_CSPICE)$(BLD_CDF),10)
build_dep:$(CSPICE_LIB)
else ifeq ($(BLD_CSPICE)$(BLD_CDF),01)
build_dep:$(CDF_LIB) $(CDF_SO)
else
build_dep: 
endif

$(CSPICE_LIB): $(BD)/cspice.tar
	cd $(BD) && tar -mxvf cspice.tar

$(CDF_SO_REDIST):$(CDF_SO)
	cp $< $@

$(BD)/cspice.tar: | $(BD)
	curl $(LOC_CSPICE_URL) > $(BD)/cspice.tar.Z
	uncompress $(BD)/cspice.tar.Z

$(CDF_LIB): $(BD)/$(LOC_CDF_DIST)
	cd $(BUILD_DIR)/$(LOC_CDF_DIST) && $(MAKE) OS=linux ENV=gnu all

$(BD)/$(LOC_CDF_DIST): | $(BD)
	curl $(LOC_CDF_URL) > $(BD)/$(LOC_CDF_DIST)-cdf.tar.gz
	cd $(BD) && tar -xvf $(LOC_CDF_DIST)-cdf.tar.gz

# Run tests
ifeq ($(BLD_CSPICE)$(BLD_CDF),11)
test:test_main test_spice test_cdf
else ifeq ($(BLD_CSPICE)$(BLD_CDF),10)
test:test_main test_spice
else ifeq ($(BLD_CSPICE)$(BLD_CDF),01)
test:test_main test_cdf
else
test:test_main
endif


test_main: $(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	env DIFFCMD=diff test/das1_fxtime_test.sh $(BD)
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
	@echo "INFO: Running unit test for dataset loader, $(BD)/das3_test..."
	@$(BD)/das3_test test/cassini_rpws_wfrm_sample.d2s
	@echo "INFO: Running unit test for credentials manager, $(BD)/TestCredMngr..."
	@$(BD)/TestCredMngr $(BD)
	@echo "INFO: Running unit test for basic das v3.0 stream parsing, $(BD)/TestV3Read..."
	$(BD)/TestV3Read
	@echo "INFO: Running unit test for ragged and unique iteration, $(BD)/TestIter..."
	$(BD)/TestIter
	@echo "INFO: Running unit test for CSVs with variable length item data, $(BD)/das3_csv"
	$(BD)/das3_csv < test/ex21_tracers_cdpu_status.d3b > $(BD)/ex21_tracers_cdpu_status.csv
	@echo "INFO: ==============================================="
	@echo "INFO: All core test programs completed without errors"
	@echo "INFO: ==============================================="		

# Can't test CDF creation this way due to stupide embedded time stamps
# cmp $(BD)/ex12_sounder_xyz.cdf test/ex12_sounder_xyz.cdf

test_cdf:$(BD) $(BD)/das3_cdf $(BD)/$(TARG).a
	@echo "INFO: Testing CDF creation"
	$(BD)/das3_cdf -l warning -i test/ex12_sounder_xyz.d3t -o $(BD) -r 
	@echo "INFO: CDF was created"

test_spice:$(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	@echo "INFO: Running unit test for spice error redirect, $(BD)/TestSpice..."
	@$(BD)/TestSpice


# Extra install if CDF lib included, downstream projects may
# need the same cdflib used by das2.
ifeq ($(BLD_CDF),1)
install:bin_install install_cdf
dev_install:bin_install cdf_install lib_install
else 
install:bin_install
dev_install:bin_install lib_install
endif

# Install everything
install_cdf:$(DESTDIR)$(INST_NAT_BIN)/das3_cdf

bin_install:$(DESTDIR)$(INST_NAT_LIB)/$(TARG).so $(INST_UTIL_PROGS)

dev_install:$(INST_NAT_LIB)/$(TARG).a $(INST_HDRS)

	
# Documentation ##############################################################
doc:$(BD)/html

$(BD)/html:$(BD) $(BD)/$(TARG).a
	@cd $(BD)
	(cat Doxyfile; echo "HTML_OUTPUT = $@ ") | doxygen -

install_doc:$(INST_DOC)/das2C

clean_doc:
	-rm -r $(BD)/html

$(INST_DOC)/das2C:$(BD)/html
	-mkdir -p $(INST_DOC)
	@if [ -e "$(INST_DOC)/das2C" ]; then rm -r $(INST_DOC)/libdas2; fi
	cp -r $(BD)/html $(INST_DOC)/das2C


# Cleanup ####################################################################
distclean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

clean:
	-rm $(BD)/das* $(BD)/Test* $(BD)/libdas*
	-rm $(BD)/*.o $(BD)/ex*
	-rm $(BD)/cred_test*


## Automatic dependency tree generation for C code ###########################

#ifneq ($(MAKECMDGOALS),clean)
#ifneq ($(MAKECMDGOALS),distclean)
#-include $(OBJS:.o=.d)
#endif
#endif




