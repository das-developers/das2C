# das2C - version 3.0

[![Anaconda Package](https://anaconda.org/dasdevelopers/das2c/badges/version.svg)](https://anaconda.org/dasdevelopers/das2c)

Das servers typically provide data relavent to space plasma and magnetospheric
physics research.  To retrieve data, an HTTP GET request is posted to a das 
server by a client program and a self-describing stream of data values covering
the requested time range, at the requested time resolution, is provided in the
response body.

This package, *das2C*, contains a portable C library  and utility
programs that provide:

  * XML schema definitions defining das2 and das3 headers.
  * Libs for Reading and writing dasStream versions 2.x and 3.X
  * General SI unit manipulation
  * Dataset accumulation
  * Generating spectrograms from time series data
  * Time averaging long duration spectrograms
  * Performing SPICE operations on data streams
  * Converting streams into export formats such as CSV and CDF.
  
The core library is used by [das2py](https://github.com/das-developers/das2py) and [das2dlm](https://github.com/das-developers/das2dlm). The utility programs are used by [dasFlex](https://github.com/das-developers/dasFlex) web-services for server-side processing.

To find out more about das2 visit https://das2.org.

## Packages: Anaconda, RPM, etc.

[![Anaconda Package](https://anaconda.org/dasdevelopers/das2c/badges/version.svg)](https://anaconda.org/DasDevelopers/das2c)

The easiest way to get das2C is just to use a package manager.  The one with the most
support is Anaconda, as das2C is required for das2py.  To install in miniconda/anaconda:

```
source miniconda3/bin/activate
conda install -c dasdevelopers das2c
```

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
$ sudo dnf install expat-devel fftw-devel openssl-devel # CentOS 7 and similar
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

If your system already has the NAIF CSpice Toolkit and the Goddard CDF libraries
installed you can set environment variables to incorporate them into the build,
otherwise the Makefiles will download them automatically.

## Manual Build

For POSIX compliant systems (Linux, MacOS, Android) issue the following commands
to build, and test the software.

```bash
$ make SPICE=yes CDF=yes
$ make SPICE=yes CDF=yes test

# To rebuild
$ make SPICE=yes CDF=yes clean     # Removes only das2C output
$ make SPICE=yes CDF=yes distclean # Removes CDF and SPICE libs as well
```

For Windows systems issue the following commands in a command shell to build, test
and install the software.

```batchfile
> set N_ARCH=\
> set LIBRARY_INC=      ::location of your vcpkg installed\x64-windows-static include 
> set LIBRARY_LIB=      ::location of your vcpkg installed\x64-windows-static lib

> vcvars.bat            ::puts nmake.exe, cl.exe, link.exe, etc. on your path

> nmake.exe /nologo /f buildfiles\Windows.mak spice=yes cdf=yes build
> nmake.exe /nologo /f buildfiles\Windows.mak spice=yes cdf=yes run_test
```

## Manual Install

If you wish to install directly from the source tree, set an install prefix and
then run make install.  For example on Linux:

```bash
$ make PREFIX=/usr/local SPICE=yes CDF=yes install   # adjust destination to taste
```
or on Windows:
```
> set INSTALL_PREFIX=C:\local   :: for example
> nmake /f buildfiles\Windows.mak SPICE=yes CDF=yes install
```

## Using the Libray

By default all header files are copied into the subdirectory `das2` under
`$PREFIX/include`.  When writing code that uses *das* headers add the top
level include directory to the compiler command line in a manner similar to
the following:
```bash
-I $PREFIX/include 
```
```batchfile
/I %PREFIX%/include
```
and use the das2 subdirectory in your include statements, for example:
```C
#include <das2/core.h>
```
Common linker arguments for building libdas dependent applications follow.
For open source programs static linking is perfectly fine:

```make
$(PREFIX)/lib/libdas.a -lexpat -lssl -lcrypto -lz -lm -lpthread                      # gnu make

$(INSTALL_PREFIX)/lib/libdas.lib  Advapi32.lib User32.lib Crypt32.lib ws2_32.lib     # win nmake
```

For closed source applications, link against shared das2 objects (i.e. libdas.so.3
or das.dll) as required by the LGPL:

```make
-L$(PREFIX)/lib -ldas -lexpat -lssl -lcrypto -lz -lm -lpthread                       # gnu make

/L $(INSTALL_PREFIX)\bin das2.3.dll das2.3.lib Advapi32.lib User32.lib Crypt32.lib ws2_32.lib  # win nmake
```

Note that on Windows, `libdas.lib` is the full static library but the file `das.lib`
is merely a DLL import library.


## Using the Utility programs

Most of the utility programs filters are designed to take a *das* stream
on standard input and output a transformed stream to standard output.  The
**das3_cdf** program



## Building with XMake (unsupported)

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

Note, the xmake files do **not** include information for building the SPICE and CDF 
components


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

