# das2C makefile for MacOS, Apple Silicon and Intel
#
# Run via the top level Makefile, which includes this file on Darwin:
#
#   make                               Static library and programs
#   make test                          Build, then run the tests
#   make PREFIX=/opt/das install       Programs and schemas
#   make PREFIX=/opt/das dev_install   Adds libdas3.a and headers
#
# The SPICE and CDF programs are built by default, add SPICE=no and/or CDF=no
# to any of the above to leave them out.
#
# Dependencies are found or fetched as follows:
#
#   fftw3, openssl:  From Homebrew (brew install fftw openssl@3).  Set
#      FFTW_DIR and OPENSSL_DIR to use copies installed elsewhere.
#
#   expat, zlib:  From the MacOS SDK, installed with the Xcode command line
#      tools.
#
#   CSPICE:  From CSPICE_INC and CSPICE_LIB when set, otherwise the NAIF
#      toolkit for this machine's architecture is downloaded to $(BD)/cspice
#
#   CDF:  From CDF_INC and CDF_LIB when set, otherwise the SPDF source
#      distribution is downloaded and built under $(BD)
#
# Apple's clang honors MACOSX_DEPLOYMENT_TARGET from the environment, set it
# to build for older MacOS versions than the one running the build.  Static
# dependencies must have been built for the same, or an older, target.

# Export name of md5sum command, the test scripts use it
MD5SUM=md5 -r
export MD5SUM

BD=$(BUILD_DIR)

##############################################################################
# Upstream Source dependencies

# arm64 or x86_64, selects the downloaded CSPICE build and the CDF build type
MAC_ARCH:=$(shell uname -m)

LOC_CDF_DIST:=cdf39_1-dist
LOC_CDF_URL:=https://spdf.gsfc.nasa.gov/pub/software/cdf/dist/cdf39_1/linux/$(LOC_CDF_DIST)-cdf.tar.gz

ifeq ($(MAC_ARCH),arm64)
LOC_CSPICE_URL:=https://naif.jpl.nasa.gov/pub/naif/toolkit/C/MacM1_OSX_clang_64bit/packages/cspice.tar.Z
LOC_CDF_ENV:=arm64
else
LOC_CSPICE_URL:=https://naif.jpl.nasa.gov/pub/naif/toolkit/C/MacIntel_OSX_AppleC_64bit/packages/cspice.tar.Z
LOC_CDF_ENV:=x86_64
endif

ifeq ($(BLD_CDF),1)
CDF_INC:=$(BD)/$(LOC_CDF_DIST)/src/include
CDF_LIB:=$(BD)/$(LOC_CDF_DIST)/src/lib/libcdf.a
endif

# Downstream packages (das2py) take libcdf.dylib from $(BD) so that they run
# the same CDF version these programs link.  It's copied there, never
# installed.  The CDF build puts the .dylib beside the .a, set CDF_SO when a
# supplied CDF_LIB doesn't have one there.
ifeq ($(CDF),yes)
CDF_SO?=$(dir $(CDF_LIB))libcdf.dylib
CDF_SO_REDIST:=$(BD)/libcdf.dylib
endif

ifeq ($(BLD_CSPICE),1)
CSPICE_INC:=./$(BD)/cspice/include
CSPICE_LIB:=./$(BD)/cspice/lib/cspice.a
endif


##############################################################################
# Project definitions

TARG=libdas3

# File lists are shared with Linux.mak and Windows.mak, add new sources there
include buildfiles/sources.mak

SRCS:=$(notdir $(LIB_SRCS))
HDRS:=$(notdir $(LIB_HDRS))

UTIL_PROGS:=$(basename $(notdir $(UTIL_SRCS)))

TEST_PROGS:=$(basename $(notdir $(TEST_SRCS)))

CDF_PROGS:=$(basename $(notdir $(CDF_UTIL_SRCS)))

ifeq ($(SPICE),yes)
UTIL_PROGS:=$(UTIL_PROGS) $(basename $(notdir $(SPICE_UTIL_SRCS)))
TEST_PROGS:=$(TEST_PROGS) $(basename $(notdir $(SPICE_TEST_SRCS)))
endif

ifeq ($(CDF),yes)
UTIL_PROGS:=$(UTIL_PROGS) $(CDF_PROGS)
endif

##############################################################################
# Build definitions

# -Wno-deprecated needed for OpenSSL, removed when call to ERR_load_BIO_strings is fixed
WARNINGS:=-Wall -Werror -Wno-deprecated-declarations

DEFINES:=-DWISDOM_FILE=/etc/fftw/wisdom -D_XOPEN_SOURCE=600

# Optimization and debug info.  Override on the command line, packagers pass
# their own flags here.
OPTFLAGS=-O2 -g


# Conda build does NOT set the include and lib directories within the
# compiler script itself, it merely exports ENV vars. This is unfortunate
# because it means makefiles must be altered to work with anaconda.

ifeq ($(CONDA_BUILD_STATE),)
# Non conda build, fftw and openssl come from Homebrew.  Use the keg paths
# under opt so that other Homebrew libraries aren't swept into the link.

CC=clang

BREW_PREFIX:=$(shell brew --prefix 2>/dev/null)
FFTW_DIR?=$(BREW_PREFIX)/opt/fftw
OPENSSL_DIR?=$(BREW_PREFIX)/opt/openssl@3

ifeq ($(wildcard $(FFTW_DIR)/include/fftw3.h),)
$(warning fftw3.h not found under $(FFTW_DIR), run: brew install fftw, or set FFTW_DIR)
endif
ifeq ($(wildcard $(OPENSSL_DIR)/include/openssl/ssl.h),)
$(warning openssl not found under $(OPENSSL_DIR), run: brew install openssl@3, or set OPENSSL_DIR)
endif

CFLAGS= $(WARNINGS) $(OPTFLAGS) $(DEFINES) -fPIC -std=c99 -I. \
 -I$(FFTW_DIR)/include -I$(OPENSSL_DIR)/include

CTESTFLAGS=$(CFLAGS)

LFLAGS= -L$(FFTW_DIR)/lib -L$(OPENSSL_DIR)/lib -lfftw3 -lexpat -lssl -lcrypto \
 -lz -lm -lpthread

else

CFLAGS:=-std=c99 $(WARNINGS) -I. -Wno-format-security $(DEFINES) $(CFLAGS)
CTESTFLAGS=-std=c99 -I. $(CFLAGS)

LFLAGS:=$(LDFLAGS) -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread

endif

# Emit a .d beside each .o naming every header that object included.  The -MP
# option adds a phony target per header so deleting one doesn't wedge the build
# with "No rule to make target".
CFLAGS:=$(CFLAGS) -MMD -MP

ifeq ($(SPICE),yes)
LFLAGS:=$(CSPICE_LIB) $(LFLAGS)
CFLAGS:=$(CFLAGS) -I$(CSPICE_INC)
endif


##############################################################################
# Derived definitions

BUILD_OBJS= $(patsubst %.c,$(BD)/%.o,$(SRCS))

UTIL_OBJS= $(patsubst %,$(BD)/%.o,$(UTIL_PROGS))

INST_HDRS= $(patsubst %.h,$(DESTDIR)$(INST_INC)/das3/%.h,$(HDRS))

BUILD_UTIL_PROGS= $(patsubst %,$(BD)/%, $(UTIL_PROGS))

INST_UTIL_PROGS= $(patsubst %,$(DESTDIR)$(INST_NAT_BIN)/%, $(UTIL_PROGS))

SCHEMAS:=$(notdir $(wildcard schema/*.xsd))

INST_SCHEMAS= $(patsubst %,$(DESTDIR)$(INST_SHARE)/das2C/%, $(SCHEMAS))

BUILD_TEST_PROGS = $(patsubst %,$(BD)/%, $(TEST_PROGS))

##############################################################################
# Pattern Rules

# Pattern rule for building object files from C source files.  Everything
# else waits on the library, and the library waits on the downloaded CSPICE
# and CDF headers, keeping parallel builds (make -j) in order.
$(BD)/%.o:das3/%.c | $(BD) build_dep
	$(CC) -c $(CFLAGS) -o $@ $<

$(BD)/%.o:utilities/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@

# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) $(CTESTFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@

# The BSD install program has no -D option for creating leading directories,
# so the install rules make them first.

# Pattern rule for installing static libraries
$(DESTDIR)$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -d -m 775 $(dir $@)
	install -m 664 $< $@

# Pattern rule for installing stream schemas
$(DESTDIR)$(INST_SHARE)/das2C/%.xsd:schema/%.xsd
	install -d -m 775 $(dir $@)
	install -m 664 $< $@

# Pattern rule for installing library header files
$(DESTDIR)$(INST_INC)/das3/%.h:das3/%.h
	install -d -m 775 $(dir $@)
	install -m 664 $< $@

# Pattern rule for installing binaries
$(DESTDIR)$(INST_NAT_BIN)/%:$(BD)/%
	install -d -m 775 $(dir $@)
	install -m 775 $< $@


# Direct make not to nuke the intermediate .o files.  Only the program objects
# are intermediates, the library objects are explicit prerequisites of the
# archive.  Listing those too would let a missing one, say the object for a
# newly added source, go unbuilt while the archive looks up to date.
.SECONDARY: $(UTIL_OBJS)
.PHONY: test build

## Explicit Rules  ###########################################################

# Programs link $(TARG).a.  No dynamic library is built, shipping one means
# supporting its ABI.
build: build_dep $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
build_static: build

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi

# Records which objects make up the library.  A source dropped from the list
# changes that set without making any object newer than the archive, so make
# alone would keep a stale one.  The file is rewritten only when the set
# differs.
$(BD)/lib_members: FORCE | $(BD)
	@echo '$(BUILD_OBJS)' | cmp -s - $@ || echo '$(BUILD_OBJS)' > $@

FORCE:
.PHONY: FORCE

$(BD)/$(TARG).a:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	rm -f $@
	ar rcs $@ $(BUILD_OBJS)

# Robert's tagged das1 reader breaks strict-aliasing expectations for C99
# and thus should not be optimized least it provide incorrect output.
# An explicit override to build a debug version instead is provided here.
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -O0 -fno-strict-aliasing -o $@ $<

# override pattern rule for das2_bin_ratesec.c, it hase two object files
$(BD)/das2_bin_ratesec:$(BD)/das2_bin_ratesec.o $(BD)/via.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

# override pattern rule for das2_psd, has extra libraries
$(BD)/das2_psd:$(BD)/das2_psd.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

cdf:$(BD)/das3_cdf $(BD)/das3_from_cdf $(BD)/das2_from_cdf

$(BD)/das3_cdf:utilities/das3_cdf.c $(BD)/$(TARG).a
	@echo "An example CDF_INC value would be: /usr/local/include"
	@echo "An example CDF_LIB value would be: /usr/local/lib/libcdf.a"
	@if [ "$(CDF_INC)" = "" ] ; then echo "CDF_INC not set"; exit 3; fi
	@if [ "$(CDF_LIB)" = "" ] ; then echo "CDF_LIB not set"; exit 3; fi
	$(CC) $(CFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)

# The CDF file model is shared by the das?_from_cdf programs
$(BD)/cdfmodel.o:utilities/cdfmodel.c utilities/cdfmodel.h $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $<

$(BD)/das3_from_cdf:utilities/das3_from_cdf.c utilities/cdfmodel.h $(BD)/cdfmodel.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/cdfmodel.o $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)

$(BD)/das2_from_cdf:utilities/das2_from_cdf.c utilities/cdfmodel.h $(BD)/cdfmodel.o $(BD)/$(TARG).a
	$(CC) $(CFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/cdfmodel.o $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)

# Dependencies to fetch and build, plus the libcdf.dylib copy however CDF got
# here
_DEPS:=
ifeq ($(BLD_CSPICE),1)
_DEPS+=$(CSPICE_LIB)
endif
ifeq ($(BLD_CDF),1)
_DEPS+=$(CDF_LIB)
endif
ifeq ($(CDF),yes)
_DEPS+=$(CDF_SO_REDIST)
endif

build_dep:$(_DEPS)

$(CSPICE_LIB): $(BD)/cspice.tar
	cd $(BD) && tar -mxf cspice.tar

# Checked on every build, see the same rule in Linux.mak for why
ifeq ($(BLD_CDF),1)
$(CDF_SO_REDIST): $(CDF_SO)

# The dylib comes out of the CDF library build, the empty recipe keeps make -j
# from looking for another way to make it
$(CDF_SO): $(CDF_LIB) ;
endif

$(CDF_SO_REDIST): FORCE | $(BD)
	@if [ ! -f "$(CDF_SO)" ]; then \
		echo "WARNING: $(CDF_SO) not found, downstream packages won't find libcdf.dylib in $(BD)"; \
		rm -f $@; \
	elif ! cmp -s "$(CDF_SO)" $@; then \
		echo "cp $(CDF_SO) $@"; cp "$(CDF_SO)" $@; \
	fi

$(BD)/cspice.tar: | $(BD)
	curl -f -L $(LOC_CSPICE_URL) > $(BD)/cspice.tar.Z
	uncompress $(BD)/cspice.tar.Z

# CDF's own tools need curses, the library does not.  Without a full Xcode
# install the CDF makefile prints xcodebuild errors while probing for an Xcode
# version, these are harmless.
$(CDF_LIB): $(BD)/$(LOC_CDF_DIST)
	cd $(BUILD_DIR)/$(LOC_CDF_DIST) && $(MAKE) OS=macosx ENV=$(LOC_CDF_ENV) \
	 CURSES=no FORTRAN=no all

$(BD)/$(LOC_CDF_DIST): | $(BD)
	curl -f -L $(LOC_CDF_URL) > $(BD)/$(LOC_CDF_DIST)-cdf.tar.gz
	cd $(BD) && tar -xf $(LOC_CDF_DIST)-cdf.tar.gz

# Run tests

# Fixture lists mirror Linux.mak; see the comment there for why both wildcards
# are load bearing and why notimp_/reject_/ex28 drop out.
V3_FIXTURES := \
 $(filter-out examples/ex28_%,$(wildcard examples/*.d3b examples/*.d3t)) \
 $(filter-out test/streams/notimp_% test/streams/reject_%,$(wildcard test/streams/*.d3b test/streams/*.d3t))

V2_FIXTURES := $(wildcard test/streams/*.d2s test/streams/*.d2t examples/*.d2s examples/*.d2t)

# The SPICE and CDF test sets run whenever those features are built in.
TEST_SETS:=test_main
ifeq ($(SPICE),yes)
TEST_SETS:=$(TEST_SETS) test_spice
endif
ifeq ($(CDF),yes)
TEST_SETS:=$(TEST_SETS) test_cdf
endif
test:$(TEST_SETS)


test_main: $(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	@echo "INFO: --- variable layer first: fastest, highest value, no goldens ---"
	@echo "INFO: Running unit test for the value layer, $(BD)/TestValue..."
	@$(BD)/TestValue
	@echo "INFO: Running unit test for the datum layer, $(BD)/TestDatum..."
	@$(BD)/TestDatum
	@echo "INFO: Running unit test for the DasGen value sources, $(BD)/TestGen..."
	@$(BD)/TestGen
	@echo "INFO: Running unit test for the DasForm formalisms, $(BD)/TestForm..."
	@$(BD)/TestForm
	@echo "INFO: Running unit test for complex arithmetic, $(BD)/TestCplx..."
	@$(BD)/TestCplx
	@echo "INFO: Running unit test for the DasVar layer, $(BD)/TestVar..."
	@$(BD)/TestVar
	@echo "INFO: Running unit test for DasVar bulk reads, $(BD)/TestVarSubset..."
	@$(BD)/TestVarSubset
	@echo "INFO: Running unit test for the DasDim container, $(BD)/TestDim..."
	@$(BD)/TestDim
	@echo "INFO: Running unit test to test units, $(BD)/TestUnits..."
	@$(BD)/TestUnits
	@echo "INFO: Running unit test for TT2000 leap seconds, $(BD)/TestTT2000..."
	@$(BD)/TestTT2000
	@echo "INFO: Running unit test for dynamic arrays, $(BD)/TestArray..."
	@$(BD)/TestArray
	@echo "INFO: --- datasets, streams and iteration ---"
	@echo "INFO: Running unit test for dataset shape/length merge, $(BD)/TestDs..."
	@$(BD)/TestDs
	@echo "INFO: Running unit test for dataset builder, $(BD)/TestBuilder..."
	@$(BD)/TestBuilder
	@echo "INFO: Running unit test for dataset loader, $(BD)/das3_info..."
	@$(BD)/das3_info -q examples/ex07_cassini_rpws_wbr.d2s
	@echo "INFO: Running unit test for stream parsing over all fixtures, $(BD)/TestV3Read..."
	$(BD)/TestV3Read test/streams/tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo "INFO: Running unit test for push parsing vs. pull parsing, $(BD)/TestFeed..."
	$(BD)/TestFeed test/streams/tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo "INFO: Running unit test for ragged and unique iteration, $(BD)/TestIter..."
	$(BD)/TestIter
	@echo "INFO: Running unit test for ragged binary re-encode, $(BD)/TestRaggedEncode..."
	$(BD)/TestRaggedEncode examples/ex30_cassini_ragged_notlast.d3b \
	examples/ex31_efi_ragged_vec.d3b examples/ex32_marsis_2d_ragged.d3b \
	examples/ex34_ragged_fixstr.d3b examples/ex38_wbr_wfrm_tags.d3b \
	examples/ex39_sandwich.d3b
	@echo "INFO: Running unit test for filter dataset ops (copy/swap), $(BD)/TestFilter..."
	$(BD)/TestFilter
	$(BD)/TestUri $(BD)/uri_tplt
	@echo "INFO: Running unit test for credentials manager, $(BD)/TestCredMngr..."
	@$(BD)/TestCredMngr $(BD)
	@echo "INFO: Running unit test for catalog sessions over local nodes, $(BD)/TestCat..."
	@$(BD)/TestCat
	@echo "INFO: Running unit test for source descriptions and requisitions, $(BD)/TestSource..."
	@$(BD)/TestSource
	@echo "INFO: Running unit test for the URL parser, $(BD)/TestHttp..."
	@$(BD)/TestHttp
	@echo "INFO: Running unit test for exception names and send.h writers, $(BD)/TestOob..."
	@$(BD)/TestOob
	$(BD)/TestOob send 2 > $(BD)/oob_send.d2s && $(BD)/TestOob read $(BD)/oob_send.d2s
	$(BD)/TestOob send 3 > $(BD)/oob_send.d3t && $(BD)/TestOob read $(BD)/oob_send.d3t
	@echo "INFO: Running unit test for v2.3.0 calls via das2_compat.h, $(BD)/TestCompat..."
	@$(BD)/TestCompat $(BD)
	CC=$(CC) test/das2_compat_poison_test.sh $(BD)  # each poison macro must stop the build
	@echo "INFO: --- golden-file comparisons (a mismatch here stops only the goldens) ---"
	env DIFFCMD=diff test/das1_fxtime_test.sh $(BD)
	test/das2_ascii_test1.sh $(BD)
	test/das2_ascii_test2.sh $(BD)
	test/das2_bin_avgsec_test1.sh $(BD)
	test/das2_bin_avgsec_test2.sh $(BD)
	test/das2_bin_avgsec_test3.sh $(BD)
	test/das2_bin_peakavgsec_test1.sh $(BD)
	test/das2_from_das1_test1.sh $(BD)
	test/das2_from_das1_test2.sh $(BD)
	test/das2_histo_test1.sh $(BD)
	test/das_prop_test.sh $(BD)
	test/das3_text_test.sh $(BD)
	test/das3_csv_test.sh $(BD)
	test/das3_info_test.sh $(BD)
	test/das3_from_das2_test.sh $(BD)
	test/das_fetch_test.sh $(BD)            # test/das_fetch/*.json nodes -> query URLs, offline
	test/das_cache_test.sh $(BD)            # node cache revalidation against a local python server
	@echo "INFO: Running unit test for CSVs with variable length item data, $(BD)/das3_csv"
	$(BD)/das3_csv < examples/ex21_tracers_cdpu_status.d3b > $(BD)/ex21_tracers_cdpu_status.csv
	@echo "INFO: ==============================================="
	@echo "INFO: All core test programs completed without errors"
	@echo "INFO: ==============================================="

# Stretch fixtures, see Linux.mak.  This target never fails the build on its own.
.PHONY: future
future: $(BD) $(BD)/TestV3Read
	@echo "INFO: Stretch fixtures we don't expect to read yet (test/streams/notimp_*):"
	@found=0; for f in test/streams/notimp_*.d3b test/streams/notimp_*.d3t; do \
		[ -e "$$f" ] || continue; found=1; \
		if $(BD)/TestV3Read "$$f" >/dev/null 2>&1; then \
			echo "  NOW PASSES -- promote it out of notimp_: $$f"; \
		else \
			echo "  unimplemented (expected): $$f"; \
		fi; \
	done; \
	[ $$found -eq 1 ] || echo "  (none present)"

# TestCdf reads CDFs back with the CDF library alone, so it links like das3_cdf
# rather than through the generic test rule.  See Linux.mak for the fixtures
# left out and why.
$(BD)/TestCdf:test/TestCdf.c $(BD)/$(TARG).a | $(BD)
	$(CC) $(CTESTFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)

test_cdf:$(BD) $(BD)/das3_cdf $(BD)/das3_from_cdf $(BD)/TestCdf $(BD)/$(TARG).a
	@echo "INFO: Testing CDF creation"
	$(BD)/das3_cdf -l warning -i examples/ex12_sounder_xyz.d3t -o $(BD) -r
	@echo "INFO: CDF was created"
	@echo "INFO: Testing composite variables in CDF output (labels, units, C-order flattening)"
	$(BD)/das3_cdf -l warning -i examples/ex15_vector_frame.d3t -o $(BD) -r
	$(BD)/das3_cdf -l warning -i examples/ex15_vector_frame.d3t -o $(BD)/ex15_mapped.cdf -r -p FRAME_NAME:COORD_FRAME
	@echo "INFO: Testing das extension attributes and a CDF round trip (ex20)"
	$(BD)/das3_cdf -l warning -i examples/ex20_tracers_aci.d3t -o $(BD) -r
	$(BD)/das3_from_cdf -l warning $(BD)/ex20_tracers_aci.cdf > $(BD)/ex20_rt.d3b
	$(BD)/das3_cdf -l warning -i $(BD)/ex20_rt.d3b -o $(BD)/ex20_rt.cdf -r
	$(BD)/das3_cdf -l warning -i examples/ex40_rotation.d3b -o $(BD) -r
	$(BD)/das3_cdf -l warning -i examples/ex41_quaternion.d3b -o $(BD) -r
	$(BD)/das3_cdf -l warning -i examples/ex42_plain_tensor.d3b -o $(BD) -r
	$(BD)/das3_cdf -l warning -i examples/ex43_msc_complex_cal.d3b -o $(BD) -r
	$(BD)/das3_cdf -l warning -i examples/ex43_msc_complex_cal.d3b -o $(BD)/ex43_keep.cdf -r -k
	$(BD)/TestCdf $(BD)

test_spice:$(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BULID_UTIL_PROGS)
	@echo "INFO: Running unit test for spice error redirect, $(BD)/TestSpice..."
	@$(BD)/TestSpice


# Programs and schemas.  With CDF=yes and SPICE=yes UTIL_PROGS already holds
# the CDF and SPICE programs.
install:bin_install

bin_install:$(INST_UTIL_PROGS) $(INST_SCHEMAS)

# What a program needs to compile and link against the library, on top of
# the above
dev_install:bin_install $(DESTDIR)$(INST_NAT_LIB)/$(TARG).a $(INST_HDRS)

.PHONY: install bin_install dev_install


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
	@if [ -e "$(INST_DOC)/das2C" ]; then rm -r $(INST_DOC)/das2C; fi
	cp -r $(BD)/html $(INST_DOC)/das2C


# Cleanup ####################################################################
distclean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

clean:
	-rm $(BD)/das* $(BD)/Test* $(BD)/libdas*
	-rm $(BD)/*.o $(BD)/*.d $(BD)/ex*
	-rm $(BD)/cred_test*


## Automatic dependency tree generation for C code ###########################

# Pull in the .d files clang wrote (see -MMD above) for automated header
# dependency tracking per *.o file
#
# On a fresh tree the wildcard finds nothing and everything gets compiled
-include $(wildcard $(BD)/*.d)
