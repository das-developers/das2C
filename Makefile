##############################################################################
# Generic definitions for: Native Programs
ifeq ($(PREFIX),)
ifeq ($(HOME),)
PREFIX:=$(USERPROFILE)
else
PREFIX=$(HOME)
endif
endif

ifeq ($(INST_ETC),)
INST_ETC=$(PREFIX)/etc
endif

ifeq ($(INST_SHARE),)
INST_SHARE=$(PREFIX)/share
endif

ifeq ($(INST_DOC),)
INST_DOC=$(INST_SHARE)/doc
endif

ifeq ($(N_ARCH),)
N_ARCH=$(shell uname -o).$(shell uname -m)
N_ARCH:=$(subst /,_,$(N_ARCH))
endif

ifeq ($(INST_INC),)
INST_INC=$(PREFIX)/include
endif

ifeq ($(INST_NAT_BIN),)
INST_NAT_BIN=$(PREFIX)/bin/$(N_ARCH)
endif

ifeq ($(INST_NAT_LIB),)
INST_NAT_LIB=$(PREFIX)/lib/$(N_ARCH)
endif

ifeq ($(SPICE),yes)
ifeq ($(CSPICE_LIB),)
$(error To add spice support set CSPICE_LIB to the absolute path of your cspice.a file)
endif
ifeq ($(CSPICE_INC),)
$(error To add spice support set CSPICE_INC to your cspice header directory)
endif
endif

C_HDR_DIR:=$(CURDIR)
BUILD_DIR:=build.$(N_ARCH)
C_BUILD_DIR:=$(CURDIR)/$(BUILD_DIR)

##############################################################################
# Native Platform specific include

UNAME=$(shell uname -s)


# Sub-Makes will need to access these directories
export PREFIX
export INST_ETC
export INST_SHARE
export INST_DOC
export INST_INC
export N_ARCH
export INST_NAT_BIN
export INST_NAT_LIB
export C_BUILD_DIR
export C_HDR_DIR

include buildfiles/$(UNAME).mak

