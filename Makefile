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
N_ARCH=/
#N_ARCH=$(shell uname).$(shell uname -m)
#N_ARCH:=$(subst /,_,$(N_ARCH))
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

BLD_CSPICE=0
ifeq ($(SPICE),yes)
ifeq ($(CSPICE_INC),)
$(info CSPICE_INC not set, download CSPICE sources if needed)
BLD_CSPICE=1
endif
ifeq ($(CSPICE_LIB),)
$(info CSPICE_LIB not set, build CSPICE from source if needed)
BLD_CSPICE=1
endif
endif

BLD_CDF=0
ifeq ($(CDF),yes)
ifeq ($(CDF_INC),)
$(info CDF_INC not set, download CDF sources if needed)
BLD_CDF=1
endif
ifeq ($(CDF_LIB),)
$(info CDF_LIB not set, build CDF lib from sources if needed)
BLD_CDF=1
endif
endif

C_HDR_DIR:=$(CURDIR)
BUILD_DIR:=build.$(N_ARCH)
C_BUILD_DIR:=$(CURDIR)/$(BUILD_DIR)

##############################################################################
# Native Platform specific include

UNAME=$(shell uname -s)
BUILD_ARCH=$(shell uname).$(shell uname -m)
BUILD_ARCH:=$(subst /,_,$(BUILD_ARCH))

$(info Host architecture is $(BUILD_ARCH))

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

# Hook for separate makefiles on Apple
ifeq ($(UNAME),Darwin)
ifeq ($(BUILD_ARCH),Darwin.arm64)
$(info Building for arm64 "M1" MacOS)
include buildfiles/Darwin.arm64.mak
else
$(info Building for intel MacOS)
include buildfiles/Darwin.mak
endif
else
include buildfiles/$(UNAME).mak
endif
