# Solaris build, mostly Linux.mak with the Sun specific parts swapped in.
# When Linux.mak changes, carry the change over here too.

# Use GNU make 
ifneq ($(MAKE),gmake)
$(error This make file is intended for use with gmake)
endif

# Export name of md5sum command (needed to make sub-scipts work on MacOS)
MD5SUM:=$(shell which md5sum)
export MD5SUM

# Set by the top level Makefile, which includes this file.  Read on its own
# (make -f) every output path would start at /, so refuse.
ifeq ($(BUILD_DIR),)
$(error buildfiles/SunOS.mak is included by Makefile, run make from the top of the source tree)
endif

BD=$(BUILD_DIR)

##############################################################################
# Upstream Source dependencies

# The CSPICE and CDF downloads Linux.mak uses are Linux builds, so nothing is
# fetched here.  Point CSPICE_INC, CSPICE_LIB, CDF_INC and CDF_LIB at local
# builds, or turn the features off with SPICE=no CDF=no.

ifeq ($(CDF),yes)
CDF_SO?=$(dir $(CDF_LIB))libcdf.so
CDF_SO_REDIST:=$(BD)/libcdf.so
endif


##############################################################################
# Project definitions

TARG=libdas3

# File lists are shared with the other makefiles, add new sources there
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

LEX=flex
YACC=bison

DEFINES:=-DWISDOM_FILE=/etc/fftw/wisdom -D_XOPEN_SOURCE=600

# OpenCSW puts its libraries and headers under /opt/csw
CSW_INC:=/opt/csw/include
CSW_LIB:=/opt/csw/lib

# The Sun Studio compiler by default, make CC=gcc for the GNU one.  DAS_API
# only marks exports under gcc (see das3/defs.h), so only a gcc build hides
# the other library symbols.
ifneq ($(findstring gcc,$(CC)),)

WARNINGS:=-Wall -Werror -Wno-deprecated-declarations -Wno-stringop-truncation \
 -Wno-format-truncation
OPTFLAGS=-O2 -g
CFLAGS:=$(WARNINGS) $(OPTFLAGS) $(DEFINES) -fPIC -std=c99 -I. -I$(CSW_INC) -MMD -MP
HIDE:=-fvisibility=hidden
SHARED:=-shared

else

CC=cc
OPTFLAGS=-xO2 -g
CFLAGS:=-xc99 -xcode=pic32 $(OPTFLAGS) $(DEFINES) -I. -I$(CSW_INC)
HIDE:=
SHARED:=-G

endif

CTESTFLAGS=$(CFLAGS)

LFLAGS:=-L$(CSW_LIB) -R$(CSW_LIB) -lfftw3 -lexpat -lssl -lcrypto -lsocket -lnsl \
 -lrt -lz -lm -lpthread

ifeq ($(SPICE),yes)
LFLAGS:=$(CSPICE_LIB) $(LFLAGS)
CFLAGS:=$(CFLAGS) -I$(CSPICE_INC)
endif

# Solaris install(1M) has different options, the GNU one is ginstall
INSTALL=ginstall


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

# Everything else waits on the library, keeping parallel builds in order
$(BD)/%.o:das3/%.c | $(BD) build_dep
	$(CC) -c $(CFLAGS) $(HIDE) -o $@ $<

$(BD)/%.o:utilities/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<
	
$(BD)/%:$(BD)/%.o | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@ 
		
# Pattern rule for building single file test and example programs
$(BD)/%:test/%.c $(BD)/$(TARG).a | $(BD)
	$(CC) $(CTESTFLAGS) $< $(BD)/$(TARG).a $(LFLAGS) -o $@

# The legacy sources are not part of the library, people copy them into their
# own programs.  They are built here only so test_legacy can show that those
# copies still compile and work against the current library.
LEGACY_OBJS:=$(BD)/legacy/das1.o $(BD)/legacy/send.o $(BD)/legacy/cli.o
LEGACY_PROGS:=$(BD)/legacy/TestCompat $(BD)/legacy/TestSend \
 $(BD)/legacy/ex_das_cli $(BD)/legacy/ex_das_ephem

# Keep the objects, otherwise make deletes them as intermediates
.SECONDARY: $(LEGACY_OBJS)

$(BD)/legacy: | $(BD)
	mkdir -p $@

$(BD)/legacy/%.o:legacy/%.c | $(BD)/legacy
	$(CC) -c $(CFLAGS) -Ilegacy -o $@ $<

$(BD)/legacy/%:legacy/%.c $(LEGACY_OBJS) $(BD)/$(TARG).a | $(BD)/legacy
	$(CC) $(CTESTFLAGS) -Ilegacy $< $(LEGACY_OBJS) $(BD)/$(TARG).a $(LFLAGS) -o $@

# Pattern rule for installing static libraries
$(DESTDIR)$(INST_NAT_LIB)/%.a:$(BD)/%.a
	$(INSTALL) -D -m 664 $< $@
	
# Pattern rule for installing stream schemas
$(DESTDIR)$(INST_SHARE)/das2C/%.xsd:schema/%.xsd
	$(INSTALL) -D -m 664 $< $@

# Pattern rule for installing library header files
$(DESTDIR)$(INST_INC)/das3/%.h:das3/%.h
	$(INSTALL) -D -m 664 $< $@	 

# Pattern rule for installing binaries
$(DESTDIR)$(INST_NAT_BIN)/%:$(BD)/%
	$(INSTALL) -D -m 775 $< $@	 
	

# Direct make not to nuke the intermediate .o files, see Linux.mak
.SECONDARY: $(UTIL_OBJS)
.PHONY: test test_more test_legacy build

## Explicit Rules  ###########################################################

build: build_dep $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
build_static: build

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi

# Records which objects make up the library, see Linux.mak
$(BD)/lib_members: FORCE | $(BD)
	@echo '$(BUILD_OBJS)' | cmp -s - $@ || echo '$(BUILD_OBJS)' > $@

FORCE:
.PHONY: FORCE

# The source's git description for das_lib_version(), see Linux.mak
GIT_DESC:=$(shell [ "$$(git rev-parse --show-toplevel 2>/dev/null)" = "$(CURDIR)" ] && git describe --tags --always --dirty 2>/dev/null)

$(BD)/git_desc: FORCE | $(BD)
	@echo '$(GIT_DESC)' | cmp -s - $@ || echo '$(GIT_DESC)' > $@

$(BD)/util.o:das3/util.c $(BD)/git_desc | $(BD) build_dep
	$(CC) -c $(CFLAGS) $(HIDE) $(if $(GIT_DESC),-DDAS_GIT_DESC='"$(GIT_DESC)"') -o $@ $<

# Start fresh, ar never drops members whose sources were renamed or deleted
$(BD)/$(TARG).a:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	rm -f $@
	ar rc $@ $(BUILD_OBJS)
	
$(BD)/$(TARG).so:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	$(CC) $(SHARED) -o $@ $(BUILD_OBJS)

# Robert's tagged das1 reader breaks strict-aliasing expectations for C99
# and thus should not be optimized least it provide incorrect output. 
ifneq ($(findstring gcc,$(CC)),)
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -O0 -fno-strict-aliasing -o $@ $<
else
$(BD)/das2_from_tagged_das1.o:utilities/das2_from_tagged_das1.c $(BD)/$(TARG).a | $(BD)
	$(CC) -c $(CFLAGS) -xO0 -xalias_level=any -o $@ $<
endif
	
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

# Nothing is downloaded on Solaris, say what to set instead
build_dep:$(if $(filter yes,$(CDF)),$(CDF_SO_REDIST))
ifeq ($(BLD_CSPICE),1)
	@echo "ERROR: Set CSPICE_INC and CSPICE_LIB to a local CSPICE build, or use SPICE=no"; exit 3
endif
ifeq ($(BLD_CDF),1)
	@echo "ERROR: Set CDF_INC and CDF_LIB to a local CDF build, or use CDF=no"; exit 3
endif

# See Linux.mak, the copy is rewritten only when the contents differ
$(CDF_SO_REDIST): FORCE | $(BD)
	@if [ ! -f "$(CDF_SO)" ]; then \
		echo "WARNING: $(CDF_SO) not found, downstream packages won't find libcdf.so in $(BD)"; \
		rm -f $@; \
	elif ! cmp -s "$(CDF_SO)" $@; then \
		echo "cp $(CDF_SO) $@"; cp "$(CDF_SO)" $@; \
	fi

# Run tests

# das3 read-regression fixtures, gathered from both stream directories.
# examples/ holds the ex??_* streams a user reads to learn the format; test/
# keeps the ones that exist only to be failed.  Two special classes drop out:
#
# 1) notimp_* examples we don't expect to read YET (see `make future`)
#
# 2) reject_* streams that are invalid by design (see test/das3_text_test.sh).
#
# ex28 is the held old-dialect reference pair (extension-codec case);
# it rejoins when the embedded= architecture lands.
V3_FIXTURES := \
 $(filter-out examples/ex28_%,$(wildcard examples/*.d3b examples/*.d3t)) \
 $(filter-out test/streams/notimp_% test/streams/reject_%,$(wildcard test/streams/*.d3b test/streams/*.d3t))

# das2 fixtures, likewise split across the two directories
V2_FIXTURES := $(wildcard test/streams/*.d2s test/streams/*.d2t examples/*.d2s examples/*.d2t)

# The SPICE and CDF test sets run whenever those features are built in.
TEST_SETS:=test_main test_legacy
ifeq ($(SPICE),yes)
TEST_SETS:=$(TEST_SETS) test_spice
endif
ifeq ($(CDF),yes)
TEST_SETS:=$(TEST_SETS) test_cdf
endif
test:$(TEST_SETS)


test_main: $(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BUILD_UTIL_PROGS)
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
	@echo "INFO: Running unit test for descriptor property storage, $(BD)/TestDesc..."
	@$(BD)/TestDesc
	@echo "INFO: Running unit test to test units, $(BD)/TestUnits..."
	@$(BD)/TestUnits
	@echo "INFO: Running unit test for TT2000 leap seconds, $(BD)/TestTT2000..." 
	@$(BD)/TestTT2000
	@echo "INFO: Running unit test for dynamic arrays, $(BD)/TestArray..."
	@$(BD)/TestArray
	@echo "INFO: --- datasets, streams and iteration ---"
	@echo "INFO: Running unit test for dataset shape/length merge, $(BD)/TestDs..."
	@$(BD)/TestDs
	@echo "INFO: Running unit test for in-memory accumulation, $(BD)/TestAccum..."
	@$(BD)/TestAccum
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
	@echo "INFO: Running unit test for byte transports, $(BD)/TestXport..."
	@$(BD)/TestXport $(BD)
	@echo "INFO: Running unit test for catalog walks over local nodes, $(BD)/TestWalk..."
	@$(BD)/TestWalk
	@echo "INFO: Running unit test for catalog sessions over local nodes, $(BD)/TestCat..."
	@$(BD)/TestCat
	@echo "INFO: Running unit test for source descriptions and requisitions, $(BD)/TestSource..."
	@$(BD)/TestSource
	@echo "INFO: Running unit test for the URL parser, $(BD)/TestHttp..."
	@$(BD)/TestHttp
	@echo "INFO: Running unit test for exception names and out-of-band writers, $(BD)/TestOob..."
	@$(BD)/TestOob
	@echo "INFO: --- golden-file comparisons (a mismatch here stops only the goldens) ---"
	env DIFFCMD=gdiff test/das1_fxtime_test.sh $(BD)  # test/das1_fxtime_output.txt (no stream fixture)
	test/das2_ascii_test1.sh $(BD)          # examples/ex04_voyager_pws_sa.d2s -> .d2t
	test/das2_ascii_test2.sh $(BD)          # test/streams/das2_swap_test.d2s -> .d2t (mixed endian)
	test/das2_asciiN_test.sh $(BD)          # generated asciiN streams, N=2..127 (issue #27)
	test/das2_repeat_header_test.sh $(BD)   # repeated das2 header under a new ID is rejected
	test/das2_bin_avgsec_test1.sh $(BD)     # examples/ex06_cassini_rpws_redef.d2s -> .d2t
	test/das2_bin_avgsec_test2.sh $(BD)     # examples/ex11_juno_fgm_scse.d2s -> .d2t
	test/das2_bin_avgsec_test3.sh $(BD)     # examples/ex09_juno_waves_survey.d2t -> .avg60.d2t
	test/das2_bin_peakavgsec_test1.sh $(BD)  # examples/ex05_vgr_pws_sa_peaks.d2s -> .d2t
	test/das2_from_das1_test1.sh $(BD)      # examples/ex01_polar_mfe.dsdf + .bin -> .d2t
	test/das2_from_das1_test2.sh $(BD)      # examples/ex02_galileo_sys3.dsdf + .bin -> .d2t
	test/das2_histo_test1.sh $(BD)          # examples/ex10_juno_waves_hfrh.d2t -> _histo.d2t
	test/das_prop_test.sh $(BD)             # test/streams/das3_test_props.d3t (its own gold)
	test/das3_text_test.sh $(BD)            # examples/ex22..ex40 (16 pairs) + test/streams/reject_*
	test/das3_csv_test.sh $(BD)             # examples/ex23_tracers_mag_hsk.d3b -> .csv
	test/das3_info_test.sh $(BD)            # examples/ex*.info golds for the dataset printers
	test/das3_from_das2_test.sh $(BD)       # examples/ex11 pass-through, metafile gold, reject
	test/das_fetch_test.sh $(BD)            # test/das_fetch/*.json nodes -> query URLs, offline
	@echo "INFO: Running unit test for CSVs with variable length item data, $(BD)/das3_csv"
	$(BD)/das3_csv < examples/ex21_tracers_cdpu_status.d3b > $(BD)/ex21_tracers_cdpu_status.csv
	@echo "INFO: ==============================================="
	@echo "INFO: All core test programs completed without errors"
	@echo "INFO: ==============================================="
#	@echo "INFO: Running unit test for catalog reader, $(BD)/TestCatalog..."
#	@echo "INFO: (network-dependent, runs last; a failure here does not affect core tests)"
#	@$(BD)/TestCatalog

# The legacy directory's copy-out files, built against the current library
test_legacy: $(LEGACY_PROGS)
	@echo "INFO: Running legacy test for v2.3.0 calls via das2_compat.h, $(BD)/legacy/TestCompat..."
	@$(BD)/legacy/TestCompat $(BD)
	CC=$(CC) legacy/das2_compat_poison_test.sh $(BD)  # each poison macro must stop the build
	@echo "INFO: Running legacy test for the send.h writers, $(BD)/legacy/TestSend..."
	$(BD)/legacy/TestSend send 2 > $(BD)/oob_send.d2s && $(BD)/legacy/TestSend read $(BD)/oob_send.d2s
	$(BD)/legacy/TestSend send 3 > $(BD)/oob_send.d3t && $(BD)/legacy/TestSend read $(BD)/oob_send.d3t

# Stretch fixtures: valid v3.0 streams that hit a read path we haven't built yet, so we
# expect them to fail today.  Kept out of 'test' (which must stay green) and parked here
# so you can watch for one starting to pass -- that's the cue to promote it out of the
# notimp_ prefix into the regular suite.  This target never fails the build on its own.
# Tests that need more than a C toolchain and bash.  Not in 'make test', so
# the base suite runs anywhere the library builds; run these by hand or in
# a job that has the extras.
#
#   das_cache_test.sh  node cache revalidation against a local HTTP server
#                      written in python3: a second run makes no request,
#                      an aged copy is renewed by a 304, a changed node is
#                      fetched again, an unreachable server leaves the old
#                      copy in use
test_more: $(BD)/das_fetch
	test/das_cache_test.sh $(BD)

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
# rather than through the generic test rule.
#
# Known exclusions from the das3_cdf fixture set:
#   ex22: a rank 3 grid with coordinates that vary along the grid indices only;
#         no ISTP DEPEND layout fits it, a bespoke converter is the answer.
#   ex27: carries a byte run (blob), which das3_cdf does not write yet.
#
$(BD)/TestCdf:test/TestCdf.c $(BD)/$(TARG).a | $(BD)
	$(CC) $(CTESTFLAGS) -Wno-unused -I$(CDF_INC) -o $@ $< $(BD)/$(TARG).a $(CDF_LIB) $(LFLAGS)

test_cdf:$(BD) $(BD)/das3_cdf $(BD)/das3_from_cdf $(BD)/das2_from_cdf $(BD)/TestCdf $(BD)/$(TARG).a
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
	test/das3_cdf_group_test.sh $(BD)       # plotGroup: dataset groups through CDF files
	$(BD)/TestCdf $(BD)

test_spice:$(BD) $(BD)/$(TARG).a $(BUILD_TEST_PROGS) $(BUILD_UTIL_PROGS)
	@echo "INFO: Running unit test for spice error redirect, ET/UTC and vsys conventions, $(BD)/TestSpice..."
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
# Everything in $(BD) is ours, so clean takes all of it.  Downloads and the
# dependency builds live in $(DEPS_DIR) and cost a re-fetch, only distclean
# goes there.  The libcdf.so copy in $(BD) comes back with the next build.
clean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

distclean: clean
	if [ -d "$(DEPS_DIR)" ] ; then rm -r $(DEPS_DIR) ; fi


## Automatic dependency tree generation for C code ###########################

# Only gcc writes the .d files (-MMD above)
ifneq ($(findstring gcc,$(CC)),)
-include $(wildcard $(BD)/*.d)
endif
