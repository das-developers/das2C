# das2C - version 2.3


Das2 servers typically provide data relavent to space plasma and magnetospheric
physics research.  To retrieve data, an HTTP GET request is posted to a das2 
server by a client program and a self-describing stream of data values covering
the requested time range, at the requested time resolution, is provided in the
response body.

This package, *das2C*, provides a portable C library, libdas2.3, which contains
functions for: 

  * Stream reading and writing
  * Streaming power spectral density estimation
  * General SI unit manipulation
  * Dataset accumulation
  * Federated catalog navigation
  
as well as a set of command line das2 stream processing programs used by [das2-pyserver](https://github.com/das-developers/das2-pyserver).

Doxygen library documentation is available in the [github pages](https://das-developers.github.io/das2C/) for 
this repository.  To find out more about das2 visit https://das2.org.

## Packages: Anaconda, RPM, etc.

[![Anaconda Package](https://anaconda.org/dasdevelopers/das2c/badges/version.svg)](https://anaconda.org/DasDevelopers/das2c)

The easiest way to get das2C is just to use a package manager.  The one with the most
support is Anaconda, as das2C is required for das2py.  To install in miniconda/anaconda:

```
source miniconda3/bin/activate
conda install -c dasdevelopers das2c
```

For Red Hat/CentOS/Rocky users you can:
```
curl -OJ https://github.com/das-developers/das2C/releases/download/v2.3.0/das2C-2.3.0-1.el8.x86_64.rpm
curl -OJ https://github.com/das-developers/das2C/releases/download/v2.3.0/das2C-devel-2.3.0-1.el8.x86_64.rpm
dnf localinstall das2C*.rpm
```

## Building the quick way: XMake

Two build systems are provided for das2C.  Plain ole GNU Make and Microsoft NMake files, and an [xmake](https://github.com/xmake-io/xmake) file.  Since xmake is both a package manager and a build tool, you do not need to install any prerequisites to build das2C with xmake, other then your compiler and xmake itself.

To build and install das2C via xmake issue:
```bash
xmake config -m debug  # or 'release' if you prefer
xmake build
xmake run unittest
xmake install          # For a POSIX style /usr/local install
#or
xmake install -o C:\local   # A windows equivalent
```

If for some reason, xmake doesn't work for you, read on for manual dependency download and build instructions.

## Manual build prequisites

Compliation of installation of das2c has been tested on Linux, Windows,
MacOS, and Android.  The following common system libraries are required to
build libdas2.3:

  * **expat** - XML Parsing library
  * **fftw3** - Fastest Fourier Transform in the West, version 3.
  * **openssl** - Secure network socket library
 
Though package names vary from system to system, commands for installing the
prequisites are provided below \.\.\.
```bash
$ sudo yum install expat-devel fftw-devel openssl-devel # CentOS 7 and similar
$ sudo yum install python3-devel python36-numpy         # CentOS 7 and similar

$ sudo apt install libexpat-dev libfftw3-dev libssl-dev zlib1g-dev    # Debian 9 and similar
```
and on windows using [vcpkg](https://github.com/microsoft/vcpkg)\.\.\.
```batchfile
> vcpkg install openssl fftw3 zlib expat pthreads --triplet x64-windows-static
```
or on mac using [brew](https://brew.sh)
```bash
$ brew install openssl
$ brew install fftw
```
The expat library should already be present on MacOS once the compiler install
command `xcode-select --install` has been run.

## Manual Build and Install

Decide where you want to install the software.  In the instructions below we've
chosen `/usr/local` but anywhere is fine. 

For POSIX compliant systems (Linux, MacOS, Android) issue the following commands
to build, test and install the software.

```
$ export PREFIX=/usr/local
$ export N_ARCH=/               # For generic builds, omit per-OS sub-directories.
$ make
$ make test
$ make install
```

For Windows systems issue the following commands in a command shell to build, test
and install the software.

```batchfile
> set N_ARCH=\
> set LIBRARY_INC=      ::location of your vcpkg installed\x64-windows-static include 
> set LIBRARY_LIB=      ::location of your vcpkg installed\x64-windows-static lib
> set INSTALL_PREFIX=C:\opt   :: for example
> nmake.exe /nologo /f buildfiles\Windows.mak build
> nmake.exe /nologo /f buildfiles\Windows.mak run_test
> nmake.exe /nologo /f buildfiles\Windows.mak install
```

## Usage

By default all header files are copied into the subdirectory `das2` under
`$PREFIX/include`.  When writing code that uses das2 headers add the include
directory to the compiler command line in a manner similar to the following:
```bash
-I $PREFIX/include 
/I %PREFIX%/include
```
and use the das2 subdirectory in your include statements, for example:
```C
#include <das2/core.h>
```
Common linker arguments for building libdas2.3 dependent applications follow.
For open source programs static linking is perfectly fine:

```make
$(PREFIX)/lib/libdas2.3.a -lexpat -lssl -lcrypto -lz -lm -lpthread                      # gnu make

$(INSTALL_PREFIX)/lib/libdas2.3.lib  Advapi32.lib User32.lib Crypt32.lib ws2_32.lib     # win nmake
```

For closed source applications, link against shared das2 objects (i.e. libdas2.3.so
or das2.3.dll) as required by the LGPL:

```make
-L$(PREFIX)/lib -ldas2.3 -lexpat -lssl -lcrypto -lz -lm -lpthread                       # gnu make

/L $(INSTALL_PREFIX)\bin das2.3.dll das2.3.lib Advapi32.lib User32.lib Crypt32.lib ws2_32.lib  # win nmake
```

Note that on Windows, `libdas2.3.lib` is the full static library but the file
`das2.3.lib` is merely a DLL import library.

## Reporting bugs
Please use the issue tracker for the [das2C](https://github.com/das-developers/das2C/issues) 
github.com project to report any problems with the library.  If you've fixed a bug, 
1) thanks!, 2) please send a pull request so that your updates can be pulled into
the main project.

## Appreciation
Though most of the code in das2C was written by das developers, the vast majority
of the code in the files json.h and json.c in the das2 subdirectory is from 
[Neil Henning's](https://github.com/sheredom) elegant and efficient JSON parser, 
[json.h](https://github.com/sheredom/json.h).  Thanks Neil for sharing your
parser with the rest of the world, it's an example of classic C programming at
it's best.

