# Export name of md5sum command (needed to make sub-scipts work on MacOS)

MD5SUM=md5sum
export MD5SUM

BD=$(BUILD_DIR)

##############################################################################
# Upstream Source dependencies

# You may already have libcdf.a and cspice.a in your build tree for other
# parts of your project.  If so use them by setting CSPICE_INC, CSPICE_LIB,
# CDF_INC and CDF_LIB.  If not, you can issue make build_dep first and
# dependency sources will be downloaded and built.

LOC_CDF_DIST:=cdf39_1-dist
LOC_CDF_URL:=https://spdf.gsfc.nasa.gov/pub/software/cdf/dist/cdf39_1/linux/$(LOC_CDF_DIST)-cdf.tar.gz
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

# File lists are shared with Windows.mak, add new sources there
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

# -Wno-deprecated needed for OpenSSL, removed when call to ERR_load_BIO_strings is fixed

# -Wno-stringop-truncation needed because I use string trucation all the time
#    intentianally and make sure it's null terminated anyway, so this error
#    doesn't apply

WARNINGS:=-Wall -Werror -Wno-deprecated-declarations -Wno-stringop-truncation

#-Wno-format-security


DEFINES:=-DWISDOM_FILE=/etc/fftw/wisdom -D_XOPEN_SOURCE=600 -Wno-format-truncation 

# Optimization and debug info.  Override on the command line, packagers pass
# their distribution's flags here.  The symbols stay in: rpm and deb builds
# split them out into separate debug packages on their own.
OPTFLAGS=-O2 -g


# Conda build does NOT set the include and lib directories within the
# compiler script itself, it merely exports ENV vars. This is unfortunate
# because it means makefiles must be altered to work with anaconda.

ifeq ($(CONDA_BUILD_STATE),)

CC=gcc 

CFLAGS= $(WARNINGS) $(OPTFLAGS) $(DEFINES) -fPIC -std=c99  -I.

#-fstack-protector-strong 

#CTESTFLAGS=-Wall -fPIC -std=c99 -ggdb -I. $(CFLAGS)
CTESTFLAGS=$(CFLAGS)

LFLAGS= -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread

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
# Library objects hide everything not marked DAS_API, so $(TARG).so exports
# what a Windows DLL does.  The same objects go into $(TARG).a, where
# visibility has no effect on a static link.
$(BD)/%.o:das3/%.c | $(BD) build_dep
	$(CC) -c $(CFLAGS) -fvisibility=hidden -o $@ $<

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
	
# Pattern rule for installing stream schemas
$(DESTDIR)$(INST_SHARE)/das2C/%.xsd:schema/%.xsd
	install -D -m 664 $< $@

# Pattern rule for installing library header files
$(DESTDIR)$(INST_INC)/das3/%.h:das3/%.h
	install -D -m 664 $< $@	 

# Pattern rule for installing binaries
$(DESTDIR)$(INST_NAT_BIN)/%:$(BD)/%
	install -D -m 775 $< $@	 
	

# Direct make not to nuke the intermediate .o files.  Only the program objects
# are intermediates, the library objects are explicit prerequisites of the
# archive.  Listing those too would let a missing one, say the object for a
# newly added source, go unbuilt while the archive looks up to date.
.SECONDARY: $(UTIL_OBJS)
.PHONY: test build

## Explicit Rules  ###########################################################

# Building dependencies if needed
#ifeq ($(BLD_CSPICE)$(BLD_CDF),00)

#build: $(BD)/$(TARG).a $(BD)/$(TARG).so $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
#build_static: $(BD) $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)

#else

# Programs link $(TARG).a.  The shared object is not part of the default build
# or of any install: shipping one means supporting its ABI.  Ask for it by
# name, make $(BD)/$(TARG).so, if you need one.
build: build_dep $(BD)/$(TARG).a $(BUILD_UTIL_PROGS) $(BUILD_TEST_PROGS) | $(BD)
build_static: build

#endif

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

# Start fresh, ar never drops members whose sources were renamed or deleted
$(BD)/$(TARG).a:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	rm -f $@
	ar rc $@ $(BUILD_OBJS)
	
$(BD)/$(TARG).so:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	gcc -shared -o $@ $(BUILD_OBJS)
	   
	
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
	curl -f -L $(LOC_CSPICE_URL) > $(BD)/cspice.tar.Z
	uncompress $(BD)/cspice.tar.Z

# CDF's own tools need curses, the library does not.  Leaving them text-only
# keeps ncurses headers off the list of things a default build requires.
$(CDF_LIB): $(BD)/$(LOC_CDF_DIST)
	cd $(BUILD_DIR)/$(LOC_CDF_DIST) && $(MAKE) OS=linux ENV=gnu CURSES=no \
	 FORTRAN=no all

# The shared library comes out of the same CDF build.  Without saying so,
# make -j looks for libcdf.so before that build has made it and stops.  The
# empty recipe keeps make from searching for some other way to build it.
$(CDF_SO): $(CDF_LIB) ;

$(BD)/$(LOC_CDF_DIST): | $(BD)
	curl -f -L $(LOC_CDF_URL) > $(BD)/$(LOC_CDF_DIST)-cdf.tar.gz
	cd $(BD) && tar -xvf $(LOC_CDF_DIST)-cdf.tar.gz

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
TEST_SETS:=test_main
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
	@echo "INFO: Running unit test for exception names and send.h writers, $(BD)/TestOob..."
	@$(BD)/TestOob
	$(BD)/TestOob send 2 > $(BD)/oob_send.d2s && $(BD)/TestOob read $(BD)/oob_send.d2s
	$(BD)/TestOob send 3 > $(BD)/oob_send.d3t && $(BD)/TestOob read $(BD)/oob_send.d3t
	@echo "INFO: Running unit test for v2.3.0 calls via das2_compat.h, $(BD)/TestCompat..."
	@$(BD)/TestCompat $(BD)
	CC=$(CC) test/das2_compat_poison_test.sh $(BD)  # each poison macro must stop the build
	@echo "INFO: --- golden-file comparisons (a mismatch here stops only the goldens) ---"
	env DIFFCMD=diff test/das1_fxtime_test.sh $(BD)  # test/das1_fxtime_output.txt (no stream fixture)
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

# Stretch fixtures: valid v3.0 streams that hit a read path we haven't built yet, so we
# expect them to fail today.  Kept out of 'test' (which must stay green) and parked here
# so you can watch for one starting to pass -- that's the cue to promote it out of the
# notimp_ prefix into the regular suite.  This target never fails the build on its own.
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

# Can't test CDF creation this way due to stupid embedded time stamps
# cmp $(BD)/ex12_sounder_xyz.cdf examples/ex12_sounder_xyz.cdf

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

# Optional test.  Run test progs under valgrind.
# Not required because valgrind isn't installed everywhere.
.PHONY: leak_test
leak_test: $(BD)/$(TARG).a $(BD)/TestArray $(BD)/TestV3Read $(BD)/TestFilter \
 $(BD)/TestGen $(BD)/TestForm $(BD)/TestCplx $(BD)/TestVar $(BD)/TestVarSubset $(BD)/TestDim $(BD)/TestDs \
 $(BD)/TestIter $(BD)/TestRaggedEncode $(BD)/TestCredMngr
	@command -v valgrind >/dev/null 2>&1 || { echo "ERROR: valgrind not found"; exit 1; }
	@rc=0; \
	for cmd in "$(BD)/TestArray" "$(BD)/TestV3Read $(V3_FIXTURES)" "$(BD)/TestFilter" \
	           "$(BD)/TestGen" "$(BD)/TestForm" "$(BD)/TestCplx" \
	           "$(BD)/TestVar" "$(BD)/TestVarSubset" "$(BD)/TestDim" "$(BD)/TestDs" "$(BD)/TestIter" \
	           "$(BD)/TestCredMngr $(BD)" \
	           "$(BD)/TestRaggedEncode examples/ex30_cassini_ragged_notlast.d3b examples/ex31_efi_ragged_vec.d3b examples/ex32_marsis_2d_ragged.d3b examples/ex34_ragged_fixstr.d3b examples/ex38_wbr_wfrm_tags.d3b examples/ex39_sandwich.d3b"; do \
		echo "INFO: valgrind $$cmd"; \
		valgrind --leak-check=full --log-file=$(BD)/leak.log $$cmd >/dev/null 2>&1; \
		grep -E "definitely lost|indirectly lost|ERROR SUMMARY" $(BD)/leak.log || true; \
		if grep -q "Invalid " $(BD)/leak.log; then \
			echo "FAIL: memory-safety violation:"; grep -A6 "Invalid " $(BD)/leak.log | head -20; rc=1; \
		fi; \
	done; \
	rm -f $(BD)/leak.log; \
	if [ $$rc -ne 0 ]; then echo "leak_test: FAILED (memory-safety violation)"; exit 1; fi; \
	echo "INFO: leak_test OK (no invalid access; review leak summaries above)"

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
distclean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

clean:
	-rm $(BD)/das* $(BD)/Test* $(BD)/libdas*
	-rm $(BD)/*.o $(BD)/ex*
	-rm $(BD)/cred_test*


## Automatic dependency tree generation for C code ###########################

# Pull in the .d files gcc wrote (see -MMD above) for automated header
# dependency tracking per *.o file
#
# On a fresh tree the wildcard finds nothing and everything gets compiled
-include $(wildcard $(BD)/*.d)




