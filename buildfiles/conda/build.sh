#!/usr/bin/env bash

# Don't differentiate output & install directories manually.  This does
# not affect which makefiles are selected only build and install directories.
N_ARCH=/

export N_ARCH

make
make test
make install
