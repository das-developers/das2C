
#  STOP!  You probably ran:
#
#     make -f buildfiles/Emscripten.mak
#
# don't do that, use the emmake wrapper instead:
#
#     source /path/to/emsdk_env.sh
#     emmake make -f buildfiles/Emscripten.mak
#
#
# But first: Clone the source for the dependencies as defined below

BD:=build.emcc

# ########################################################################### #
# Expat #

# To get all expat sources...
#
#   cd ../   (aka the directory containing das2C)
#   git clone git@github.com:libexpat/libexpat.git
#   sudo apt install libtool
#   ../libexpat/expat
#   ./buildconf.sh
#   ./configure

ifeq ($(EXPAT_DIR),)
EXPAT_DIR=$(PWD)/../libexpat
endif

EXPAT_TARG=libexpat.a
EXPAT_SRCS:=xmlparse.c xmltok.c xmlrole.c

EXPAT_OBJS= $(patsubst %.c,$(BD)/%.o,$(EXPAT_SRCS))

# ########################################################################### #
# OpenSSL #

# To get all the openssl sources and build them properly
# cd ../
# git clone https://github.com/openssl/openssl.git
# cd openssl
# source $HOME/git/emsdk/emsdk_env.sh
# emconfigure ./Configure -no-asm -static -no-afalgeng -no-apps
# emmake make CC=/home/cwp/git/emsdk/upstream/emscripten/emcc AR=/home/cwp/git/emsdk/upstream/emscripten/emar
#
# This should result in libssl.a and libcrypto.a in the root of your openssl 
# directory.  The rest of the make file begins from that assumption.

ifeq ($(SSL_DIR),)
SSL_DIR=$(PWD)/../openssl
endif

SSL_LIB:=$(SSL_DIR)/libssl.a
CRYPTO_LIB:=$(SSL_DIR)/libcrypto.a
SSL_INC:=$(SSL_DIR)/include

# ########################################################################### #
# Das 3 #

DAS_TARG=libdas.a

DAS_SRCS:=das1.c array.c buffer.c builder.c cli.c codec.c credentials.c dataset.c \
datum.c descriptor.c dimension.c dsdf.c encoding.c frame.c http.c io.c \
iterator.c json.c log.c oob.c operator.c node.c packet.c plane.c processor.c \
property.c serial.c send.c stream.c time.c tt2000.c units.c utf8.c util.c \
value.c variable.c vector.c

NOT_YET:=dft.c 

DAS_OBJS= $(patsubst %.c,$(BD)/%.o,$(DAS_SRCS))


TEST_PROGS:=TestUnits TestArray TestVariable LoadStream TestBuilder \
 TestAuth TestCatalog TestTT2000 ex_das_cli ex_das_ephem TestCredMngr \
 TestV3Read

BUILD_TEST_PROGS = $(patsubst %,$(BD)/%.js, $(TEST_PROGS))


# ########################################################################### #
# Tools

CFLAGS=-g -I. -I$(EXPAT_DIR)/expat/lib -I$(SSL_INC) -s USE_ZLIB=1

LFLAGS=$(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) $(SSL_LIB) $(CRYPTO_LIB) -lz -lm -lpthread

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
$(BD)/%.js:test/%.c $(BD)/$(DAS_TARG) $(BD)/$(EXPAT_TARG) | $(BD)
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

# Cleanup ####################################################################

distclean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi

clean:
	if [ -d "$(BD)" ]; then rm -r $(BD); fi

