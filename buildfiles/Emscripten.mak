##############################################################################
# WebAssembly cross build via emscripten
#
# Selected by the top level Makefile when EMSCRIPTEN=yes:
#
#    make EMSCRIPTEN=yes EXPAT_INC=/path/to/expat/lib build
#    make EMSCRIPTEN=yes EXPAT_INC=... EXPAT_LIB=/path/to/libexpat.a test
#
# Builds the library archive and, for 'test', a subset of the test programs
# as node.js scripts.  No utilities, no shared object, no program install.
# The archive is meant to be linked into a wasm module by a downstream
# project that owns the JavaScript side (das2js).
#
# Prerequisites:
#
#   emsdk activated in the calling shell so emcc and emar are on PATH.  The
#   bundled node is found through EMSDK_NODE, which emsdk_env.sh exports.
#
#   EXPAT_INC  directory holding expat.h.  Emscripten ships a zlib port but
#              no expat port, so the caller supplies one built for wasm32.
#   EXPAT_LIB  the matching libexpat.a, needed only to link test programs.
#
# What is left out, and why:
#
#   http.c credentials.c node.c   sockets and OpenSSL do not exist in a
#                                 browser; fetch() on the JS side replaces them
#   dft.c                         FFTW is not built for wasm yet.  DFT=yes
#                                 puts it back given FFTW_INC.
#   cli.c send.c                  program scaffolding, nothing a library
#                                 consumer needs
#
# Dropping those files leaves dangling calls in util.c, io.c and buffer.c.
# Two feature macros close them: DAS_NO_NETWORK compiles out the socket and
# SSL stream modes, DAS_NO_DFT skips FFTW setup in das_init().  They are
# defined here and nowhere else, so every native build is unaffected.

# Cleaning needs neither the compiler nor the expat location
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(shell command -v emcc 2>/dev/null),)
$(error emcc not found on PATH.  Activate emsdk first: source /path/to/emsdk/emsdk_env.sh)
endif
endif

BD=$(BUILD_DIR)

##############################################################################
# Project definitions

TARG=libdas3

include buildfiles/sources.mak

NET_SRCS:=das3/http.c das3/credentials.c das3/node.c
PROG_SRCS:=das3/cli.c das3/send.c
DFT_SRCS:=das3/dft.c

WASM_SRCS:=$(filter-out $(NET_SRCS) $(PROG_SRCS),$(LIB_SRCS))
FEATURE_DEFS:=-DDAS_NO_NETWORK

ifeq ($(DFT),yes)
ifeq ($(FFTW_INC),)
$(error DFT=yes needs FFTW_INC, the directory holding fftw3.h built for wasm32)
endif
DEP_INC:=-I$(FFTW_INC)
else
WASM_SRCS:=$(filter-out $(DFT_SRCS),$(WASM_SRCS))
FEATURE_DEFS:=$(FEATURE_DEFS) -DDAS_NO_DFT
endif

ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(EXPAT_INC),)
$(error EXPAT_INC is not set.  Point it at a directory holding expat.h built for wasm32)
endif
endif
DEP_INC:=$(DEP_INC) -I$(EXPAT_INC)

SRCS:=$(notdir $(WASM_SRCS))
HDRS:=$(notdir $(LIB_HDRS))

# Test programs that need nothing from the excluded sources.  Anything that
# opens a URL, reads credentials, or needs SPICE stays out.
TEST_PROGS:=TestValue TestDatum TestGen TestForm TestCplx TestVar \
 TestVarSubset TestDim TestUnits TestTT2000 TestArray TestDs TestIter \
 TestRaggedEncode TestFilter TestUri TestV3Read TestFeed

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
# beside it
$(BD)/%.js:test/%.c $(BD)/$(TARG).a | $(BD)
	@if [ -z "$(EXPAT_LIB)" ]; then echo "ERROR: EXPAT_LIB not set, needed to link test programs"; exit 3; fi
	$(CC) $(CFLAGS) $< $(BD)/$(TARG).a $(EXPAT_LIB) $(TEST_LFLAGS) -o $@

$(DESTDIR)$(INST_NAT_LIB)/%.a:$(BD)/%.a
	install -D -m 664 $< $@

$(DESTDIR)$(INST_INC)/das3/%.h:das3/%.h
	install -D -m 664 $< $@

# No .SECONDARY here, unlike Linux.mak: nothing is an intermediate.  Objects
# are explicit prereqs of the archive and the test .js files link straight from
# test/*.c.  
.PHONY: build test install dev_install clean distclean

##############################################################################
# Explicit rules

build: $(BD)/$(TARG).a

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); mkdir $(BD); chmod g+w $(BD); fi

# The member set depends on DFT, so start the archive fresh rather than let
# ar keep stale objects from an earlier configuration
$(BD)/$(TARG).a:$(BUILD_OBJS) | $(BD)
	rm -f $@
	$(AR) rc $@ $(BUILD_OBJS)

test: $(BD)/$(TARG).a $(BUILD_TEST_PROGS)
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
	$(NODE) $(BD)/TestUri.js $(BD)/uri_tplt
	@echo "INFO: ==============================================="
	@echo "INFO: All wasm test programs completed without errors"
	@echo "INFO: ==============================================="

# Only the archive and headers: there are no programs to install
dev_install: $(DESTDIR)$(INST_NAT_LIB)/$(TARG).a $(INST_HDRS)
install: dev_install

distclean:
	if [ -d "$(BD)" ] ; then rm -r $(BD) ; fi

clean:
	-rm $(BD)/*.o $(BD)/*.d $(BD)/*.js $(BD)/*.wasm $(BD)/libdas*

# Header dependencies written by -MMD above
-include $(wildcard $(BD)/*.d)
