# Export mac compatable md5sum command

export MD5SUM := md5 -r
export DIFFCMD := diff

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

BD=$(BUILD_DIR)

##############################################################################
# Build definitions

CC=gcc

DEFINES=-DWISDOM_FILE=/etc/fftw/wisdom
WARNINGS:=-Wall -Wno-format-security -Wno-format-truncation

# Conda build does NOT set the include and lib directories within the
# compiler script itself, it merely exports ENV vars. This is unfortunate
# because it means makefiles must be altered to work with anaconda.

ifeq ($(CONDA_BUILD_STATE),)
# Non conda build, depends on homebrew

BREW_INC_DIR=/usr/local/include
BREW_LIB_DIR=/usr/local/lib


ifeq ($(OPENSSL_DIR),)
OPENSSL_DIR=/usr/local/opt/openssl
endif

SSL_INC=-I $(OPENSSL_DIR)/include
SSL_LIB=$(OPENSSL_DIR)/lib/libssl.a $(OPENSSL_DIR)/lib/libcrypto.a

CFLAGS=-ggdb -fPIC -std=c99 -Wno-format-security -I. $(SSL_INC) $(DEFINES) -I$(BREW_INC_DIR)
#CFLAGS=-Wall -DNDEBUG -O2 -fPIC -std=c99 -I. $(DEFINES)

CTESTFLAGS=-ggdb -Wall -fPIC -std=c99 -I. $(SSL_INC)

LFLAGS= -L$(BREW_LIB_DIR) -lfftw3 -lexpat $(SSL_LIB) -lz -lm -lpthread

else
# Conda build, dependencies handle by downstream meta.yaml
CC:=clang -arch x86_64

SSL_INC=
SSL_LIB=-lssl -lcrypto

#CFLAGS:=-std=c99 -I. -Wno-format-security $(DEFINES) $(CFLAGS)
CFLAGS:=-std=c99 -I. $(SSL_INC) $(DEFINES) $(CFLAGS)
CTESTFLAGS=-Wall -fPIC -std=c99 -I. $(CFLAGS)

LFLAGS:=$(LDFLAGS) -lfftw3 -lexpat $(SSL_LIB) -lz -lm -lpthread

endif

ifeq ($(SPICE),yes)
LFLAGS:=$(CSPICE_LIB) $(LFLAGS)
CFLAGS:=$(CFLAGS) -I$(CSPICE_INC)
endif

##############################################################################
# Derived definitions

BUILD_OBJS= $(patsubst %.c,$(BD)/%.o,$(SRCS))

UTIL_OBJS= $(patsubst %,$(BD)/%.o,$(UTIL_PROGS))

INST_HDRS= $(patsubst %.h,$(INST_INC)/das2/%.h,$(HDRS))

BUILD_UTIL_PROGS= $(patsubst %,$(BD)/%, $(UTIL_PROGS))

INST_UTIL_PROGS= $(patsubst %,$(INST_NAT_BIN)/%, $(UTIL_PROGS))

BUILD_TEST_PROGS = $(patsubst %,$(BD)/%, $(TEST_PROGS))

##############################################################################
# Pattern Rules

# Pattern rules building C source lib code to object files
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
$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -m 775 -d $(INST_NAT_LIB)
	install -m 664 $< $@	 

# Pattern rule for installing dynamic libraries
$(INST_NAT_LIB)/%.so:$(BD)/%.so
	install -m 775 -d $(INST_NAT_LIB)
	install -m 775 $< $@	

# Pattern rule for installing library header files
$(INST_INC)/das2/%.h:das2/%.h
	install -m 775 -d $(INST_INC)/das2
	install -m 664 $< $(INST_INC)/das2

# Pattern rule for installing binaries
$(INST_NAT_BIN)/%:$(BD)/%
	install -m 775 -d $(INST_NAT_BIN)
	install -m 775 $< $(INST_NAT_BIN)
	

# Direct make not to nuke the intermediate .o files
.SECONDARY: $(BUILD_OBJS) $(UTIL_OBJS)
.PHONY: test

## Explicit Rules  ###########################################################

build:$(BD) $(BD)/$(TARG).a $(BD)/$(TARG).so \
 $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS)

build_static:$(BD) $(BD)/$(TARG).a \
 $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS)

$(BD)/$(TARG).a:$(BUILD_OBJS)
	ar rc $@ $(BUILD_OBJS)

$(BD)/$(TARG).so:$(BUILD_OBJS)
	$(CC) -shared $(BUILD_OBJS) $(LFLAGS) -o $@ 

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi
	   	
# Robert's tagged das1 reader breaks strict-aliasing expectations for C99
# and thus should not be optimized least it provide incorrect output. 
# An explicit override to build a debug version instead is provided here.
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c -Wall -std=c99 -I. -ggdb -o $@ $<
	
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


# Run tests
test: $(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	test/das1_fxtime_test.sh $(BD)
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
	@echo "INFO: Running unit test for credentials manager, $(BD)/TestCredMngr..."
	@$(BD)/TestCredMngr $(BD)
	@echo "INFO: All test programs completed without errors"
	$(BD)/TestV3Read
	@echo "INFO: Running unit test for ragged and unique iteration, $(BD)/TestIter..."
	$(BD)/TestIter
	@echo "INFO: All test programs completed without errors"

# Can't test CDF creation this way due to stupid embedded time stamps
# cmp $(BD)/ex12_sounder_xyz.cdf test/ex12_sounder_xyz.cdf

test_cdf:$(BD) $(BD)/das3_cdf $(BD)/$(TARG).a
	@echo "INFO: Testing CDF creation"
	$(BD)/das3_cdf -l warning -i test/ex12_sounder_xyz.d3t -o $(BD) -r 
	@echo "INFO: CDF was created"

test_spice:$(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	@echo "INFO: Running unit test for spice error redirect, $(BD)/TestSpice..."
	@$(BD)/TestSpice


# Install everything
install:lib_install $(INST_UTIL_PROGS)

install_cdf:$(INST_NAT_BIN)/das3_cdf

lib_install:$(INST_NAT_LIB)/$(TARG).a $(INST_NAT_LIB)/$(TARG).so $(INST_HDRS)

# Does not install static object that that it can be used with proprietary
# software
so_install:$(INST_NAT_LIB)/$(TARG).so $(INST_HDRS)
	
# Documentation ##############################################################
doc:$(BD)/html

$(BD)/html:$(BD) $(BD)/$(TARG).a
	@cd $(BD)
	(cat Doxyfile; echo "HTML_OUTPUT = build.Linux.x86_64/html") | doxygen -

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
