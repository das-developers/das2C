##############################################################################
# WebAssembly cross build via emscripten
#
# Its own entry point, like windows.mak: the top level Makefile never reads
# this file.  Run from the top of the source tree:
#
#    make -f buildfiles/emscripten.mak              Same as build
#    make -f buildfiles/emscripten.mak build        Static library
#    make -f buildfiles/emscripten.mak build_test   Adds the test programs
#    make -f buildfiles/emscripten.mak test         Build, then run the tests
#    make -f buildfiles/emscripten.mak PREFIX=/opt/das dev_install
#    make -f buildfiles/emscripten.mak clean        Removes build.wasm32
#    make -f buildfiles/emscripten.mak distclean    Also removes deps.wasm32
#
# Builds the library archive and, for 'build_test' and 'test', a subset of
# the test programs as node.js scripts; 'build_test' links them without
# running them.  No utilities, no shared object, no program install.
# The archive is meant to be linked into a wasm module by a downstream
# project that owns the JavaScript side (das2js).
#
# Prerequisites:
#
#   emsdk activated in the calling shell so emcc and emar are on PATH.  The
#   bundled node is found through EMSDK_NODE, which emsdk_env.sh exports.
#
#   Network access on the first build, see below.
#
# Dependencies.  Emscripten ships a zlib port (--use-port=zlib) but no expat
# or FFTW, so both are built here for wasm32, the way CDF and CSPICE are for
# native builds: when a pair below is unset, the release tarball is fetched
# from its *_URL, checked against its *_SHA256, and configured, built and
# installed under $(DEPS_DIR).  Only distclean removes them.  Set a pair to
# use an existing wasm32 build instead, as absolute paths (or paths relative
# to this directory):
#
#   EXPAT_INC, EXPAT_LIB  directory holding expat.h, and libexpat.a
#   FFTW_INC, FFTW_LIB    directory holding fftw3.h, and libfftw3.a
#
# What is left out, and why:
#
#   http.c credentials.c catalog.c   sockets, OpenSSL and the node cache do
#                                 not exist in a browser.  fetch() on the JS
#                                 side drives DasNodeWalk instead; node.c,
#                                 source.c and req.c never touch a transport
#   cli.c send.c                  program scaffolding, nothing a library
#                                 consumer needs
#
# Dropping the network sources leaves dangling calls in util.c, io.c and
# buffer.c.  DAS_NO_NETWORK compiles those out; das3/defs.h defines it
# whenever __EMSCRIPTEN__ is, so nothing here passes it.
#
# dft.c is in by default.  DFT=no (or any value but "yes") leaves it out and
# defines DAS_NO_DFT, which skips FFTW setup in das_init().  FFTW is GPL, so
# a module linked with it is too, see THIRD-PARTY.txt.

# Every path below is relative to the top of the source tree
ifeq ($(wildcard buildfiles/sources.mak),)
$(error Run make -f buildfiles/emscripten.mak from the top of the das2C source tree)
endif

# Cleaning doesn't need the compiler
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(shell command -v emcc 2>/dev/null),)
$(error emcc not found on PATH.  Activate emsdk first: source /path/to/emsdk/emsdk_env.sh)
endif
endif

##############################################################################
# Output and install locations

# One directory per target, as in the native builds.  The downloads and
# dependency builds sit beside the build directory rather than in it, so
# clean can remove all of $(BD) and only distclean costs a re-download.
N_ARCH=wasm32
BD:=build.$(N_ARCH)
DEPS_DIR:=deps.$(N_ARCH)

# Same defaults as the top level Makefile.  Only dev_install uses them, and
# the archive goes under lib/wasm32 so it never lands on a native one.
ifeq ($(PREFIX),)
PREFIX=$(HOME)
endif

ifeq ($(INST_INC),)
INST_INC=$(PREFIX)/include
endif

ifeq ($(INST_NAT_LIB),)
INST_NAT_LIB=$(PREFIX)/lib/$(N_ARCH)
endif

##############################################################################
# Project definitions

TARG=libdas3

include buildfiles/sources.mak

NET_SRCS:=das3/http.c das3/credentials.c das3/catalog.c
PROG_SRCS:=das3/cli.c das3/send.c
DFT_SRCS:=das3/dft.c

WASM_SRCS:=$(filter-out $(NET_SRCS) $(PROG_SRCS),$(LIB_SRCS))
FEATURE_DEFS:=

DFT?=yes

# Pulled from the maintainer's site so the downloads count for them.  If a
# build fails because fftw.org is down, our department mirror has the same
# file:
#
#   make FFTW_URL=https://space.physics.uiowa.edu/spdr/packages/source/$(FFTW_DIST).tar.gz
#
# Upstream publishes only an MD5.  The SHA-256 was taken from an fftw.org
# copy that matched it, and holds either source to the release itself.
FFTW_DIST:=fftw-3.3.11
FFTW_URL?=https://www.fftw.org/$(FFTW_DIST).tar.gz
FFTW_SHA256:=5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1

# shasum -a 256 on macOS
SHA256SUM?=sha256sum

BLD_FFTW=0
ifeq ($(DFT),yes)
ifeq ($(FFTW_INC),)
BLD_FFTW=1
endif
ifeq ($(FFTW_LIB),)
BLD_FFTW=1
endif
endif

ifeq ($(BLD_FFTW),1)
FFTW_INC:=$(DEPS_DIR)/fftw/include
FFTW_LIB:=$(DEPS_DIR)/fftw/lib/libfftw3.a
endif

ifeq ($(DFT),yes)
DEP_INC:=-I$(FFTW_INC)
else
WASM_SRCS:=$(filter-out $(DFT_SRCS),$(WASM_SRCS))
FEATURE_DEFS:=-DDAS_NO_DFT
endif

# Pulled from the maintainer's GitHub releases, as with FFTW above.  If that
# is down, our department mirror has the same file:
#
#   make EXPAT_URL=https://space.physics.uiowa.edu/spdr/packages/source/$(EXPAT_DIST).tar.gz
#
# The SHA-256 is the one GitHub lists for the release asset
EXPAT_VER:=2.8.5
EXPAT_DIST:=expat-$(EXPAT_VER)
EXPAT_URL?=https://github.com/libexpat/libexpat/releases/download/R_$(subst .,_,$(EXPAT_VER))/$(EXPAT_DIST).tar.gz
EXPAT_SHA256:=920dde485e15eda0cce8d2310b41d492c534e5e3d89ad407a0b4176dd2ff88fe

BLD_EXPAT=0
ifeq ($(EXPAT_INC),)
BLD_EXPAT=1
endif
ifeq ($(EXPAT_LIB),)
BLD_EXPAT=1
endif

ifeq ($(BLD_EXPAT),1)
EXPAT_INC:=$(DEPS_DIR)/expat/include
EXPAT_LIB:=$(DEPS_DIR)/expat/lib/libexpat.a
endif

DEP_INC:=$(DEP_INC) -I$(EXPAT_INC)

SRCS:=$(notdir $(WASM_SRCS))
HDRS:=$(notdir $(LIB_HDRS))

# Test programs that need nothing from the excluded sources.  Anything that
# opens a URL, reads credentials, or needs SPICE stays out.
TEST_PROGS:=TestValue TestDatum TestGen TestForm TestCplx TestVar \
 TestVarSubset TestDim TestDesc TestUnits TestTT2000 TestArray TestDs TestIter \
 TestRaggedEncode TestFilter TestUri TestV3Read TestFeed TestWalk

##############################################################################
# Build definitions

CC=emcc
AR=emar

# emcc is clang: the gcc-only -Wno-stringop-truncation of Linux.mak would
# itself be an error here
WARNINGS:=-Wall -Werror -Wno-deprecated-declarations

DEFINES:=-D_XOPEN_SOURCE=600 $(FEATURE_DEFS)

OPTFLAGS=-O2 -g

# --use-port=zlib supplies zlib.h at compile time and the library at link
# time.  -MMD -MP writes a .d beside each .o for header dependency tracking,
# same scheme as Linux.mak.
CFLAGS=$(WARNINGS) $(OPTFLAGS) $(DEFINES) -std=c99 -I. $(DEP_INC) \
 --use-port=zlib -MMD -MP

# Test programs run under node with the real filesystem mounted (NODERAWFS)
# so fixtures need no embedding.  Flag notes:
# * EXIT_RUNTIME makes main()'s return value the process exit status
# * The 1 MiB stack is native-like; emscripten defaults to 64 KiB.  
# * -g keeps DWARF, which makes emcc warn that wasm-opt ran fewer passes
#      with -Werror on the same command line that would fail the link.
#
TEST_LFLAGS=--use-port=zlib -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 \
 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 \
 -Wno-limited-postlink-optimizations

ifeq ($(NODE),)
ifeq ($(EMSDK_NODE),)
NODE:=node
else
NODE:=$(EMSDK_NODE)
endif
endif

##############################################################################
# Derived definitions

BUILD_OBJS=$(patsubst %.c,$(BD)/%.o,$(SRCS))

BUILD_TEST_PROGS=$(patsubst %,$(BD)/%.js,$(TEST_PROGS))

INST_HDRS=$(patsubst %.h,$(DESTDIR)$(INST_INC)/das3/%.h,$(HDRS))

# Fixture lists mirror Linux.mak, see the comment there for the exclusions
V3_FIXTURES := \
 $(filter-out examples/ex28_%,$(wildcard examples/*.d3b examples/*.d3t)) \
 $(filter-out test/streams/notimp_% test/streams/reject_%,$(wildcard test/streams/*.d3b test/streams/*.d3t))

V2_FIXTURES := $(wildcard test/streams/*.d2s test/streams/*.d2t examples/*.d2s examples/*.d2t)

##############################################################################
# Pattern rules

$(BD)/%.o:das3/%.c | $(BD)
	$(CC) -c $(CFLAGS) -o $@ $<

# emcc picks the output kind from the suffix: a .js loader with the .wasm
# beside it.  The dependency libraries are prerequisites so that a rebuilt
# or caller-supplied one relinks these.  FFTW_LIB is empty with DFT off.
$(BD)/%.js:test/%.c $(BD)/$(TARG).a $(EXPAT_LIB) $(FFTW_LIB) | $(BD)
	$(CC) $(CFLAGS) $< $(BD)/$(TARG).a $(EXPAT_LIB) $(FFTW_LIB) $(TEST_LFLAGS) -o $@

$(DESTDIR)$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -D -m 664 $< $@

$(DESTDIR)$(INST_INC)/das3/%.h:das3/%.h
	install -D -m 664 $< $@

# No .SECONDARY here, unlike Linux.mak: nothing is an intermediate.  Objects
# are explicit prereqs of the archive and the test .js files link straight from
# test/*.c.  
.PHONY: build build_test test install dev_install clean distclean

##############################################################################
# Explicit rules

build: $(BD)/$(TARG).a

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); mkdir $(BD); chmod g+w $(BD); fi

$(DEPS_DIR):
	@if [ ! -e "$(DEPS_DIR)" ]; then echo mkdir $(DEPS_DIR); mkdir $(DEPS_DIR); chmod g+w $(DEPS_DIR); fi

# expat, when not supplied ###################################################

ifeq ($(BLD_EXPAT),1)
# The sources that include expat.h, which the install below puts in place
$(BD)/dataset_hdr3.o $(BD)/oob.o $(BD)/packet.o $(BD)/stream.o: | $(EXPAT_LIB)
endif

$(DEPS_DIR)/$(EXPAT_DIST).tar.gz: | $(DEPS_DIR)
	curl -f -L $(EXPAT_URL) > $@.part
	echo "$(EXPAT_SHA256)  $@.part" | $(SHA256SUM) -c -
	mv $@.part $@

# Order-only, see the FFTW rule below for why
$(DEPS_DIR)/$(EXPAT_DIST)/configure: | $(DEPS_DIR)/$(EXPAT_DIST).tar.gz
	cd $(DEPS_DIR) && tar -xzf $(EXPAT_DIST).tar.gz

# Static library only, no xmlwf, examples, tests or man pages.  Parser
# features stay at expat's defaults (DTD, general entities, namespaces, 1024
# context bytes), the same as the system expat native builds link, so both
# parse alike.  The hash salt comes from getentropy() alone, which emscripten
# implements over crypto.getRandomValues (browser) or crypto.randomFillSync
# (node).  Every other entropy source is refused: the browser module has no
# /dev/urandom, and a probe that guessed wrong would fail at runtime, not
# here.
#
# emconfigure is needed, emmake is not.  configure records the full paths
# of emcc, emar and emranlib in the Makefiles it writes, so a plain make
# already builds for wasm.  emmake would also cost make -j: it is a Python
# wrapper, Python closes inherited file descriptors when it starts a
# program, and those carry make's job slots, so the sub-make warns
# "jobserver unavailable" and runs one job at a time.  The FFTW rule below
# is where that hurts.
$(EXPAT_LIB): $(DEPS_DIR)/$(EXPAT_DIST)/configure
	cd $(DEPS_DIR)/$(EXPAT_DIST) && emconfigure ./configure \
	 --prefix=$(CURDIR)/$(DEPS_DIR)/expat --disable-shared --enable-static \
	 --without-xmlwf --without-examples --without-tests --without-docbook \
	 --with-getentropy --without-getrandom --without-sys-getrandom \
	 --without-arc4random --without-arc4random-buf --without-dev-urandom
	cd $(DEPS_DIR)/$(EXPAT_DIST) && $(MAKE) install

# FFTW, when not supplied ####################################################

ifeq ($(BLD_FFTW),1)
# Only dft.c includes fftw3.h, which the install below puts in place
$(BD)/dft.o: | $(FFTW_LIB)
endif

# Kept only once the hash matches, so a short or swapped download never
# looks finished to make
$(DEPS_DIR)/$(FFTW_DIST).tar.gz: | $(DEPS_DIR)
	curl -f -L $(FFTW_URL) > $@.part
	echo "$(FFTW_SHA256)  $@.part" | $(SHA256SUM) -c -
	mv $@.part $@

# Extracted files keep the release's time stamps, which autotools reads:
# stamped all alike (tar -m), Makefile.am looks no older than Makefile.in
# and make tries to rerun automake.  So configure is older than the tarball,
# and the prerequisite is order-only to keep that from forcing a re-extract.
$(DEPS_DIR)/$(FFTW_DIST)/configure: | $(DEPS_DIR)/$(FFTW_DIST).tar.gz
	cd $(DEPS_DIR) && tar -xzf $(FFTW_DIST).tar.gz

# Static, double precision only: dft.c uses the fftw_ API alone.  No threads
# (this wasm build is single threaded), no Fortran, no docs.  The install
# prefix is where FFTW_INC and FFTW_LIB point.  CFLAGS is what configure
# picks on its own minus -mtune=native, which clang ignores for wasm with a
# warning per file, well over a thousand of them.
#
# SIMD: FFTW's generic-simd128 codelets are written with compiler vector
# extensions, and -msimd128 has clang lower them to wasm SIMD, which every
# current browser and node runs (Firefox since 89).  If a build or a runtime
# fails over it, drop both --enable-generic-simd128 and -msimd128, then make
# distclean.
#
# ac_cv_func_clock_gettime=no is not optional.  With clock_gettime found,
# kernel/cycle.h takes clock_gettime(CLOCK_SGI_CYCLE) as its tick counter
# because musl defines that constant, but no wasm runtime has the clock.
# The ticks never advance and FFTW_MEASURE planning loops forever.  Without
# it FFTW has no tick counter and plans by estimate, its own fallback.
# Nothing else in FFTW uses clock_gettime.
$(FFTW_LIB): $(DEPS_DIR)/$(FFTW_DIST)/configure
	cd $(DEPS_DIR)/$(FFTW_DIST) && emconfigure ./configure \
	 --prefix=$(CURDIR)/$(DEPS_DIR)/fftw --disable-shared --enable-static \
	 --disable-fortran --disable-threads --disable-doc \
	 --enable-generic-simd128 ac_cv_func_clock_gettime=no \
	 CFLAGS="-O3 -fomit-frame-pointer -fstrict-aliasing -msimd128"
	cd $(DEPS_DIR)/$(FFTW_DIST) && $(MAKE) install

# Records which objects make up the library.  DFT= changes that set without
# making any object newer than the archive, so make alone would keep a stale
# one.  The file is rewritten only when the set differs.
$(BD)/lib_members: FORCE | $(BD)
	@echo '$(BUILD_OBJS)' | cmp -s - $@ || echo '$(BUILD_OBJS)' > $@

FORCE:
.PHONY: FORCE

# The member set depends on DFT, so start the archive fresh rather than let
# ar keep stale objects from an earlier configuration
$(BD)/$(TARG).a:$(BUILD_OBJS) $(BD)/lib_members | $(BD)
	rm -f $@
	$(AR) rc $@ $(BUILD_OBJS)

# Link only, run nothing: lets a parent build make the test programs in its
# default target and run them later
build_test: $(BD)/$(TARG).a $(BUILD_TEST_PROGS)

test: build_test
	@echo "INFO: wasm test programs run under $(NODE)"
	@echo "INFO: Running unit test for the value layer, $(BD)/TestValue..."
	@$(NODE) $(BD)/TestValue.js
	@echo "INFO: Running unit test for the datum layer, $(BD)/TestDatum..."
	@$(NODE) $(BD)/TestDatum.js
	@echo "INFO: Running unit test for the DasGen value sources, $(BD)/TestGen..."
	@$(NODE) $(BD)/TestGen.js
	@echo "INFO: Running unit test for the DasForm formalisms, $(BD)/TestForm..."
	@$(NODE) $(BD)/TestForm.js
	@echo "INFO: Running unit test for complex arithmetic, $(BD)/TestCplx..."
	@$(NODE) $(BD)/TestCplx.js
	@echo "INFO: Running unit test for the DasVar layer, $(BD)/TestVar..."
	@$(NODE) $(BD)/TestVar.js
	@echo "INFO: Running unit test for DasVar bulk reads, $(BD)/TestVarSubset..."
	@$(NODE) $(BD)/TestVarSubset.js
	@echo "INFO: Running unit test for the DasDim container, $(BD)/TestDim..."
	@$(NODE) $(BD)/TestDim.js
	@echo "INFO: Running unit test for descriptor property storage, $(BD)/TestDesc..."
	@$(NODE) $(BD)/TestDesc.js
	@echo "INFO: Running unit test to test units, $(BD)/TestUnits..."
	@$(NODE) $(BD)/TestUnits.js
	@echo "INFO: Running unit test for TT2000 leap seconds, $(BD)/TestTT2000..."
	@$(NODE) $(BD)/TestTT2000.js
	@echo "INFO: Running unit test for dynamic arrays, $(BD)/TestArray..."
	@$(NODE) $(BD)/TestArray.js
	@echo "INFO: Running unit test for dataset shape/length merge, $(BD)/TestDs..."
	@$(NODE) $(BD)/TestDs.js
	@echo "INFO: Running unit test for stream parsing over all fixtures, $(BD)/TestV3Read..."
	$(NODE) $(BD)/TestV3Read.js test/streams/tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo "INFO: Running unit test for push parsing vs. pull parsing, $(BD)/TestFeed..."
	$(NODE) $(BD)/TestFeed.js test/streams/tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo "INFO: Running unit test for ragged and unique iteration, $(BD)/TestIter..."
	$(NODE) $(BD)/TestIter.js
	@echo "INFO: Running unit test for ragged binary re-encode, $(BD)/TestRaggedEncode..."
	$(NODE) $(BD)/TestRaggedEncode.js examples/ex30_cassini_ragged_notlast.d3b \
	examples/ex31_efi_ragged_vec.d3b examples/ex32_marsis_2d_ragged.d3b \
	examples/ex34_ragged_fixstr.d3b examples/ex38_wbr_wfrm_tags.d3b \
	examples/ex39_sandwich.d3b
	@echo "INFO: Running unit test for filter dataset ops (copy/swap), $(BD)/TestFilter..."
	$(NODE) $(BD)/TestFilter.js
	@echo "INFO: Running unit test for URI templates, $(BD)/TestUri..."
	$(NODE) $(BD)/TestUri.js $(BD)/uri_tplt
	@echo "INFO: Running unit test for catalog walks over local nodes, $(BD)/TestWalk..."
	@$(NODE) $(BD)/TestWalk.js
	@echo "INFO: ==============================================="
	@echo "INFO: All wasm test programs completed without errors"
	@echo "INFO: ==============================================="

# Only the archive and headers: there are no programs to install
dev_install: $(DESTDIR)$(INST_NAT_LIB)/$(TARG).a $(INST_HDRS)
install: dev_install

# All of $(BD) is ours; $(DEPS_DIR) holds the expat and FFTW downloads and
# builds
clean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

distclean: clean
	if [ -d "$(DEPS_DIR)" ] ; then rm -r $(DEPS_DIR) ; fi

# Header dependencies written by -MMD above
-include $(wildcard $(BD)/*.d)
