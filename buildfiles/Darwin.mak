# Export mac compatable md5sum command

export MD5SUM := md5 -r
export DIFFCMD := diff

##############################################################################
# Project definitions

TARG=libdas2.3.a

SRCS:=das1.c array.c buffer.c builder.c cli.c credentials.c dataset.c datum.c \
descriptor.c dft.c dimension.c dsdf.c encoding.c http.c io.c json.c log.c \
node.c oob.c operator.c packet.c plane.c processor.c send.c stream.c time.c \
tt2000.c units.c utf8.c util.c value.c variable.c
 
HDRS:=defs.h time.h das1.h util.h log.h buffer.h utf8.h value.h units.h \
 tt2000.h operator.h datum.h array.h encoding.h variable.h descriptor.h \
 dimension.h dataset.h plane.h packet.h stream.h processor.h oob.h io.h \
 builder.h dsdf.h credentials.h http.h dft.h json.h node.h cli.h send.h core.h

ifeq ($(SPICE),yes)
SRCS:=$(SRCS) spice.c
HDRS:=$(HDRS) spice.h
endif

UTIL_PROGS=das1_inctime das2_prtime das1_fxtime das2_ascii das2_bin_avg \
 das2_bin_avgsec das2_bin_peakavgsec das2_from_das1 das2_from_tagged_das1 \
 das1_ascii das1_bin_avg das2_bin_ratesec das2_psd das2_hapi das2_histo \
 das2_cache_rdr das_node

TEST_PROGS:=TestUnits TestArray TestVariable LoadStream TestBuilder \
 TestAuth TestCatalog TestTT2000 ex_das_cli ex_das_ephem TestCredMngr

ifeq ($(SPICE),yes)
TEST_PROGS:=$(TEST_PROGS) TestSpice
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
# Conda build

SSL_INC=
SSL_LIB=-lssl -lcrypto

#CFLAGS:=-std=c99 -I. -Wno-format-security $(DEFINES) $(CFLAGS)
CFLAGS:=-std=c99 -I. $(SSL_INC) $(DEFINES) $(CFLAGS)
CTESTFLAGS=-Wall -fPIC -std=c99 -I. $(CFLAGS)

LFLAGS:=$(LDFLAGS) -lfftw3 -lexpat $(SSL_LIB) -lz -lm -lpthread

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

$(BD)/%.o:utilities/%.c $(BD)/$(TARG) | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 
		
# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c $(BD)/$(TARG) | $(BD)
	$(CC) $(CTESTFLAGS) $< $(BD)/$(TARG) $(LFLAGS) -o $@ 

# Pattern rule for installing static libraries
$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -m 775 -d $(INST_NAT_LIB)
	install -m 664 $< $@	 

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

build:$(BD) $(BD)/$(TARG) $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS)

$(BD)/$(TARG):$(BUILD_OBJS)
	ar rc $@ $(BUILD_OBJS)

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi
	   	
# Robert's tagged das1 reader breaks strict-aliasing expectations for C99
# and thus should not be optimized least it provide incorrect output. 
# An explicit override to build a debug version instead is provided here.
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG) | $(BD)
	$(CC) -c -Wall -std=c99 -I. -ggdb -o $@ $<
	
# override pattern rule for das2_bin_ratesec.c, it hase two object files
$(BD)/das2_bin_ratesec:$(BD)/das2_bin_ratesec.o $(BD)/via.o $(BD)/$(TARG)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@ 

# override pattern rule for das2_psd, has extra libraries
$(BD)/das2_psd:$(BD)/das2_psd.o $(BD)/send.o $(BD)/$(TARG)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@ 
	

# Run tests
test: $(BD) $(BD)/$(TARG) $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
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


# Install C-lib and C Utilities
install:$(INST_NAT_LIB)/$(TARG) $(INST_HDRS) $(INST_UTIL_PROGS)
	
# Documentation ##############################################################
doc:$(BD)/html

$(BD)/html:$(BD) $(BD)/$(TARG)
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
