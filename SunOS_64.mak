# makefile for libdas.a ( -L/local/lib -ldas )

# Pick a default install location, if user doesn't have one defiened
ifeq ($(PREFIX),)
PREFIX=/local
endif

# Have a seperate build directory for each arch so that we won't accidentally
# install libs for the wrong platform

ARCH=$(shell uname -s).$(shell uname -p)
BUILD_DIR:=build.$(ARCH)64

INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib/sparcv9

CFLAGS= -O -m64

SRCS=parsetime.c ttime.c daspkt.c
HDRS=das.h

OBJS= $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

##############################################################################
# Pattern rules

.SUFFIXES:

$(BUILD_DIR)/%.o:%.c |  $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -o $@ $<


##############################################################################
# Explicit rules

all: $(BUILD_DIR)/libdas.a

$(BUILD_DIR):
	@if [ ! -d "$(BUILD_DIR)" ] ; then \
	echo mkdir $(BUILD_DIR); \
	mkdir -p $(BUILD_DIR); \
	fi

$(BUILD_DIR)/libdas.a: $(OBJS)
	ar rv $@ $(OBJS)

install: $(BUILD_DIR)/libdas.a
	mkdir -p $(LIBDIR)
	mkdir -p $(INCDIR)
	install $(BUILD_DIR)/libdas.a $(LIBDIR) 
	install das.h $(INCDIR)
	
pylib_install:
	@echo 64-bit build of python module daslib is a bad idea. 

clean:
	rm -f -r $(BUILD_DIR) build
