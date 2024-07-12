
#  STOP!  You probably ran:
#
#     make -f buildfiles/Emscripten.mak
#
# don't do that, use the emmake wrapper instead:
#
#     source /path/to/emsdk_env.sh
#     emmake make -f buildfiles/Emscripten.mak
#
# But first: Clone the source for the dependencies as defined below
#
# Also, if you don't have emscipten, then follow set it below.

# ########################################################################### #
# System prequisites # 

# sudo apt install libtool

# ########################################################################### #
# Emscripten #

# curl https://github.com/emscripten-core/emsdk/archive/refs/tags/3.1.63.tar.gz > emsdk-3.1.63.tar.gz
# cd /usr/local/
# sudo tar -xvzf emsdk-3.1.63.tar.gz
# sudo mv emsdk-3.1.63 emsdk  # It will lose it's version numbers anyway after updates
#
# cd emsdk
# ./emsdk list    # <-- this command is your friend, use it often
# ./emsdk install latest  # Get's Node 18 on linux
# ./emsdk activate latest
# 
# source  emsdk_env.sh

# ./emsdk install emscripten-main-32bit
# ./emsdk activate emscripten-main-32bit
# ./emsdk install node-16.20.0-64bit
# ./emsdk activate node-16.20.0-64bit
# ./emsdk install binaryen-main-64bit
# ./emsdk activate binaryen-main-64bit
# ./emsdk install llvm-git-main-32bit  (Big job run on server or go to lunch)
# ./emsdk activate llvm-git-main-32bit
#
# source emsdk_env.sh
# /emscripten/main/bootstrap.py

# Lastly, EMSCRIPTEN can't find the system python, so symlink it
#
# ln -s $(which python3) python  # In root of emscripten directory

BD:=build.emcc

ifeq ($(NODE_JS),)
NODE_JS:=node
endif

NODE_JS:=/usr/local/emsdk/node/18.20.3_64bit/bin/node

# ########################################################################### #
# Expat #

# Here and below $HOME/git is assumed to be the location of your git clones, 
# adjust accordingly. 
#
# To setup libexpat...
#
#   cd $HOME/git
#   git clone git@github.com:libexpat/libexpat.git
#   cd libexpat/expat
#   emconfigure ./buildconf.sh
#   emconfigure ./configure  --without-docbook # Do NOT use emconfigure here!

ifeq ($(EXPAT_DIR),)
EXPAT_DIR=$(PWD)/../libexpat
endif

EXPAT_TARG=libexpat.a
EXPAT_SRCS:=xmlparse.c xmltok.c xmlrole.c

EXPAT_OBJS= $(patsubst %.c,$(BD)/%.o,$(EXPAT_SRCS))

# ########################################################################### #
# OpenSSL #

# Build this as a library.
#
# cd $HOME/git
# git clone https://github.com/openssl/openssl.git
# cd openssl
# source $HOME/git/emsdk/emsdk_env.sh
# emconfigure ./Configure -no-asm -static -no-afalgeng -no-apps
# 
# Notes:
# * emmake doesn't set $CC and $AR correctly, I have no idea why)
#
# * newer versions of emscriptin probably include stdatomic so
#   CFLAGS=-D__STDC_NO_ATOMICS__=1 is probably not needed
#  
# emmake make CC=$EMSCRIPTEN/emcc AR=$EMSCRIPTEN/emar RANLIB=$EMSCRIPTEN/emranlib CFLAGS=-D__STDC_NO_ATOMICS__=1
#
# This should result in libssl.a and libcrypto.a in the root of your openssl 
# directory.  The rest of the make file begins from that assumption.

# ifeq ($(SSL_DIR),)
# SSL_DIR=$(PWD)/../openssl
# endif
# 
# SSL_LIB:=$(SSL_DIR)/libssl.a
# CRYPTO_LIB:=$(SSL_DIR)/libcrypto.a
# SSL_INC:=$(SSL_DIR)/include

# ########################################################################### #
# Das 3 #

DAS_TARG=libdas.a

DAS_SRCS:=das1.c array.c buffer.c builder.c cli.c codec.c credentials.c dataset.c \
datum.c descriptor.c dimension.c dsdf.c encoding.c frame.c io.c \
iterator.c json.c log.c oob.c operator.c packet.c plane.c processor.c \
property.c serial.c send.c stream.c time.c tt2000.c units.c utf8.c util.c \
value.c variable.c vector.c

NOT_YET:=dft.c node.c
NOT_EVER:=http.c

DAS_OBJS= $(patsubst %.c,$(BD)/%.o,$(DAS_SRCS))

TEST_PROGS:=TestUnits TestArray TestVariable LoadStream TestBuilder \
 TestTT2000 ex_das_cli ex_das_ephem TestCredMngr \
 TestV3Read

NOT_YET_PROGS:=TestAuth TestCatalog

BUILD_TEST_PROGS = $(patsubst %,$(BD)/%.html, $(TEST_PROGS))


# ########################################################################### #
# Tools

CC=emcc

#CFLAGS=-g -I. -I$(EXPAT_DIR)/expat/lib -I$(SSL_INC) -s USE_ZLIB=1
CFLAGS=-g -I. -I$(EXPAT_DIR)/expat/lib -s USE_ZLIB=1

#LFLAGS=$(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) $(SSL_LIB) $(CRYPTO_LIB) -lz -lm -lpthread
LFLAGS=$(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) -lz -lm -lpthread

# ########################################################################### #
# Pattern rules

# Pattern rule for building LLVM bitcode files from C source files
$(BD)/%.o:das2/%.c | $(BD)
	$(CC) -c $(CFLAGS) --no-entry -o $@ $<

$(BD)/%.o:$(EXPAT_DIR)/expat/lib/%.c
	$(CC) -c $(CFLAGS) -I$(EXPAT_DIR)/expat --no-entry -o $@ $<	

$(BD)/%.o:utilities/%.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) | $(BD)
	$(CC) -c $(CFLAGS) $< $(LFLAGS) -o $@ 

# Pattern rule for building single file test and example programs
$(BD)/%.html:test/%.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) | $(BD)
	$(CC) $(CFLAGS) $< $(LFLAGS) -o $@


# ########################################################################### #
# Explicit rules

#build:$(BD) $(BD)/$(TARG).wasm $(BD)/$(TARG).js
build:$(BD) $(BD)/$(EXPAT_TARG) $(BD)/$(DAS_TARG) $(BUILD_TEST_PROGS)

$(BD):
	@if [ ! -e "$(BD)" ]; then echo mkdir $(BD); \
        mkdir $(BD); chmod g+w $(BD); fi

$(BD)/$(DAS_TARG):$(DAS_OBJS)
	$(AR) rc $@ $^

$(BD)/$(EXPAT_TARG):$(EXPAT_OBJS)
	$(AR) rc $@ $^

# Compiling in test files (which seems wierd)
$(BD)/TestBuilder.html:test/TestBuilder.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) $(BD)
	$(CC) $(CFLAGS) $< $(LFLAGS)  -o $@ \
--preload-file test/galileo_pws_sample.d2t \
--preload-file test/x_multi_y.d2s \
--preload-file  test/cassini_rpws_sample.d2t \
--preload-file  test/juno_waves_sample.d2t \
--preload-file test/mex_marsis_bmag.d2t \
--preload-file test/cassini_rpws_wfrm_sample.d2s \
--preload-file test/ex12_sounder_xyz.d3t \
--preload-file test/ex15_vector_frame.d3b \
--preload-file test/ex17_vector_noframe.d3b

$(BD)/LoadStream.html:test/LoadStream.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) $(BD)
	$(CC) $(CFLAGS) $< $(LFLAGS)  -o $@ \
--preload-file test/cassini_rpws_wfrm_sample.d2s

$(BD)/TestV3Read.html:test/TestV3Read.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) $(BD)
	$(CC) $(CFLAGS) $< $(LFLAGS)  -o $@ \
--preload-file test/tag_test.dNt \
--preload-file test/ex12_sounder_xyz.d3t


# Run tests
test: $(BD) $(BD)/$(DAS_TARG) $(BUILD_TEST_PROGS)
	@echo "INFO: Running unit test to test units, $(BD)/TestUnits..." 
	cd $(BD) && $(NODE_JS) TestUnits.js
	@echo "INFO: Running unit test for dynamic arrays, $(BD)/TestArray..."
	cd $(BD) && $(NODE_JS) TestArray.js
	@echo "INFO: Running unit test for index space mapping, $(BD)/TestVariable..."
	cd $(BD) && $(NODE_JS) TestVariable.js
	@echo "INFO: Running unit test for dataset builder, $(BD)/TestBuilder..."
	cd $(BD) && $(NODE_JS) TestBuilder.js
	@echo "INFO: Running unit test for dataset loader, $(BD)/LoadStream..."
	cd $(BD) && $(NODE_JS) LoadStream.js
	@echo "INFO: Running unit test for credentials manager, $(BD)/TestCredMngr..."
	cd $(BD) && $(NODE_JS) TestCredMngr.js .
	@echo "INFO: Running unit test for basic das v3.0 stream parsing, $(BD)/TestV3Read..."
	cd $(BD) && $(NODE_JS) TestV3Read.js
	@echo "INFO: Running unit test for TT2000 leap seconds, $(BD)/TestTT2000..." 
	cd $(BD) && $(NODE_JS) TestTT2000.js
	@echo "INFO: All test programs completed without errors"


# Doesn't work yet
#	@echo "INFO: Running unit test for catalog reader, $(BD)/TestCatalog..."
#	@$(NODE) $(BD)/TestCatalog.js


# Cleanup ####################################################################

distclean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi

clean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi

