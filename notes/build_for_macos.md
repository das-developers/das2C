# Building das2C on MacOS

The same makefile builds das2C on Apple Silicon (arm64) and Intel (x86_64)
Macs.  In short:

1. Install the Xcode command line tools
2. Install Homebrew, then fftw and openssl
3. Clone das2C and run `make`

The CSPICE and CDF libraries are downloaded and built by the makefile unless
`SPICE=no` and `CDF=no` are given.  A full Xcode install is not needed.


## 1. Install the Xcode command line tools

Log in on the Mac directly, not over ssh, and run:

```bash
$ xcode-select --install
```

This opens a graphical installer.  Afterwards check that the compiler is
present:

```bash
$ clang --version
```

The command line tools provide clang, GNU make 3.81, git, and the expat and
zlib libraries.  Note that the `gcc` command on MacOS is clang under another
name.


## 2. Install Homebrew and the prerequisites

Follow the instructions at <https://brew.sh>, then:

```bash
$ brew install fftw openssl@3
```

Homebrew installs under `/opt/homebrew` on Apple Silicon and `/usr/local` on
Intel.  The makefile asks `brew --prefix` which one applies.  To use fftw or
openssl from somewhere else, set `FFTW_DIR` and `OPENSSL_DIR` to the
directories holding their `include` and `lib` subdirectories.


## 3. Build and test

```bash
$ git clone https://github.com/das-developers/das2C.git
$ cd das2C
$ make
$ make test
```

Add `SPICE=no` and/or `CDF=no` to skip those programs.  To use
existing copies of the toolkits, set `CSPICE_INC`, `CSPICE_LIB`, `CDF_INC` and
`CDF_LIB`, otherwise:

* CSPICE: The NAIF toolkit for this machine's architecture is downloaded to
  `build./cspice`.

* CDF: The SPDF source distribution is downloaded and compiled under
  `build./cdf39_1-dist`.  While it builds, the CDF makefile prints a few
  `xcodebuild requires Xcode` errors and `ld: warning: search path` messages
  as it probes for a full Xcode install.  These are harmless.

To start over:

```bash
$ make clean       # Removes only das2C output
$ make distclean   # Removes CDF and SPICE libs as well
```


## 4. Install

```bash
$ make PREFIX=/usr/local install      # programs, schemas
$ make PREFIX=/usr/local dev_install  # adds libdas3.a, headers
```

`DESTDIR` is honored for staged installs.  The programs link fftw and openssl
from Homebrew dynamically, so keep those packages installed.  CSPICE and CDF
are linked statically.

To link your own programs against `libdas3.a` add the Homebrew library
directories to the link line:

```make
$(PREFIX)/lib/libdas3.a -L/opt/homebrew/opt/fftw/lib -L/opt/homebrew/opt/openssl@3/lib \
  -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread
```


## Building for older MacOS versions

clang targets the running MacOS version by default.  Set
`MACOSX_DEPLOYMENT_TARGET` in the environment to build for older ones, for
example when making Python wheels:

```bash
$ MACOSX_DEPLOYMENT_TARGET=11.0 make
```

Libraries linked into the result, including Homebrew's, must have been built
for the same or an older target.  The linker warns when they were not.


## MacOS differences to keep in mind

Some command line tools on MacOS come from BSD rather than GNU and behave a
bit differently.  The test scripts and makefile account for these:

* `md5 -r` stands in for `md5sum`.  The makefile exports `MD5SUM`.
* `install` has no `-D` option, so install rules make directories first.
* `wc` pads its counts with leading spaces.
* `sed -i` requires an argument, use `sed -i ''` for in place edits.
