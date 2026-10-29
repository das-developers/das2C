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

In addition [DeepWiki](https://deepwiki.com/das-developers/das2C) provides a
reasonable overview  of das2C. The AI generated containment and inheritance 
diagrams are actually quite useful.

[![DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/das-developers/das2C)

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
build libdas3:

  * [**expat**](https://libexpat.github.io/) - XML Parsing library
  * [**fftw3**](https://www.fftw.org/) - Fastest Fourier Transform in the West, version 3.
  * [**openssl**](https://www.openssl.org/) - Secure network socket library
  * [**zlib**](https://www.zlib.net/) - Compression and decompression
  * **pthreads** - Thread synchronization, part of the system libraries on
    Linux, MacOS and Android.  On Windows the [pthreads4w](https://sourceforge.net/projects/pthreads4w/)
    port is used.
 
Though package names vary from system to system, commands for installing the
prequisites are provided below \.\.\.
```bash
$ sudo dnf install expat-devel fftw-devel openssl-devel # CentOS 7 and similar
$ sudo apt install libexpat-dev libfftw3-dev libssl-dev zlib1g-dev    # Debian 9 and similar
```
On Windows the makefile installs these itself using [vcpkg](https://github.com/microsoft/vcpkg),
see the Manual Build section below.  On mac use [brew](https://brew.sh)\.\.\.
```bash
$ brew install fftw openssl@3
```
The expat and zlib libraries are already present on MacOS once the compiler
install command `xcode-select --install` has been run.  See
[notes/build_for_macos.md](notes/build_for_macos.md) for details.

If your system already has the NAIF CSpice Toolkit and the Goddard CDF libraries
installed you can set environment variables to incorporate them into the build,
otherwise the Makefiles will download them automatically.  The variables are
`CSPICE_INC` and `CDF_INC` for the header directories, and `CSPICE_LIB` and
`CDF_LIB` for the static library files themselves (not their directories).

## Manual Build

For POSIX compliant systems (Linux, MacOS, Android) issue the following commands
to build, and test the software.

```bash
$ make
$ make test

# To rebuild
$ make clean     # Removes only das2C output
$ make distclean # Removes CDF and SPICE libs as well
```

The SPICE and CDF programs are built by default.  To leave them out, and skip
downloading those libraries, add `SPICE=no` and/or `CDF=no` to each command.

For a WebAssembly build of the library alone (no utilities), with emscripten's
emsdk activated in the shell:

```bash
$ make -f buildfiles/emscripten.mak
$ make -f buildfiles/emscripten.mak test

# To rebuild
$ make -f buildfiles/emscripten.mak clean     # Removes only das2C output
$ make -f buildfiles/emscripten.mak distclean # Removes expat and FFTW as well
```

Output lands in `build.wasm32/`.  The build expat and fftw and builds them
for wasm32 under `deps.wasm32/`.  See [notes/build_for_emscripten.md](notes/build_for_emscripten.md) for the toolchain setup.

For Windows systems issue the following commands in a command shell to build
and test the software.  Dependencies are installed by [vcpkg](https://vcpkg.io)
from `vcpkg.json`, and the CDF and SPICE libraries are downloaded, as on Linux.
See [notes/build_for_windows.md](notes/build_for_windows.md) for details.

```batchfile
> vcvars64.bat              ::puts nmake.exe, cl.exe, link.exe, etc. on your path
> set VCPKG_ROOT=           ::location of your vcpkg clone

> nmake /f buildfiles\windows.mak
> nmake /f buildfiles\windows.mak test
```

On Windows you can also build with [CMake](https://cmake.org), which Visual
Studio and VS Code pick up directly.  The CMake build includes SPICE and CDF by
default and puts its output under `build.cmake\<preset>`.  It has only been
tested on Windows so far.

```batchfile
> vcvars64.bat
> set VCPKG_ROOT=                 ::location of your vcpkg clone
> cmake --preset default          ::configure step, vcpkg builds the dependencies
> cmake --build --preset default
> ctest --preset default
```

As with nmake, this builds `das3.dll` and the programs run on it.  See
[notes/build_for_windows.md](notes/build_for_windows.md#5-building-with-cmake)
for options and details.

## Manual Install

If you wish to install directly from the source tree, set an install prefix and
then run make install.  For example on Linux:

```bash
$ make PREFIX=/usr/local install   # adjust destination to taste
```
or on Windows:
```batch
nmake /f buildfiles\windows.mak PREFIX=C:\opt install  
```
```batch
cmake --install build.cmake\default --prefix C:\opt
```

> **Side note:** a CMake install can be undone with
> `cmake --build --preset default --target uninstall`.  This removes only the
> files from the *most recent* install out of that build directory, as recorded
> in its `install_manifest.txt`.  Files from earlier installs to other
> prefixes and the top-level directories under the prefix (`bin`, `lib`, etc.)
> are not removed. Check the prefix afterwards if you need it completely clean.

## Using the Libray

The `dev_install` target copies the static library to `$PREFIX/lib` and all
header files into the subdirectory `das3` under `$PREFIX/include`.  When writing
code that uses *das* headers add the top level include directory to the compiler
command line in a manner similar to the following:
```bash
-I $PREFIX/include 
```
```batchfile
/I %PREFIX%/include
```
and use the das3 subdirectory in your include statements, for example:
```C
#include <das3/core.h>
```
Common linker arguments for building libdas3 dependent applications follow.
For open source programs static linking is perfectly fine:

```make
$(PREFIX)/lib/libdas3.a -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread       # gnu make

$(PREFIX)/lib/libdas3.a -L/opt/homebrew/opt/fftw/lib -L/opt/homebrew/opt/openssl@3/lib \
  -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread     # mac, /usr/local on Intel

$(PREFIX)\lib\libdas3.lib libexpatMD.lib fftw3.lib zs.lib libssl.lib \
  libcrypto.lib pthreadVC3.lib Advapi32.lib User32.lib Crypt32.lib ws2_32.lib  # win nmake
```

The Windows build compiles everything with the `cl.exe` option `/MD`, so compile
your code with `/MD` as well (the default for most Visual Studio and CMake
projects).  `/MD` links a program to the C runtime DLLs, `vcruntime140.dll`
and the Universal CRT that's part of Windows.  The alternative, `/MT`, copies
the C runtime into each program and DLL instead.  Each copy then has its own
heap and its own `stdout`, so memory allocated in one module can't be freed in
another and a `FILE*` can't be shared between them, both of which das2C needs.
Mixing the two in one program gets you linker warnings at best.

For closed source applications, link against the shared library (i.e. libdas3.so
or das3.dll) as required by the LGPL.  On Linux the shared library isn't part
of the default build, ask for it by name with `make $(BD)/libdas3.so`.  On
Windows the nmake build always makes `das3.dll`, since the programs use it, and
`PREFIX=... dll_install` installs it along with its import library and headers.

```make
-L$(PREFIX)/lib -ldas3 -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread          # gnu make

/DDAS_USE_DLL ... $(PREFIX)\lib\das3.lib                                       # win nmake
```

On Windows, code compiled against the DLL must define `DAS_USE_DLL` so that the
headers mark library functions as imported, and `das3.dll` must be on the `PATH`
at run time.  Note that `libdas3.lib` is the full static library but the file
`das3.lib` is merely a DLL import library.

A CMake install also adds a CMake package, so CMake projects can skip the
flags above.  Add the install prefix to `CMAKE_PREFIX_PATH`, then:

```cmake
find_package(das2C CONFIG REQUIRED)
target_link_libraries(myprog PRIVATE das2C::das3)          # static library
target_link_libraries(myprog PRIVATE das2C::das3_shared)   # DLL, dll preset only
```

The `das2C::das3_shared` target defines `DAS_USE_DLL` for you.


## Using the Utility programs

Most of the utility programs filters are designed to take a *das* stream
on standard input and output a transformed stream to standard output.  Except
for **das3_cdf**. It reads das2 or 3 streams and outputs CDF files, which
aren't a streamable format.  For those, a temp file is built and then piped 
to stdout after all input is complete.  All the rest:

* das2_psd
* das2_ascii
* das2_bin_avgsec
* das3_text
* das3_csv
* das3_spice

process data packet by packet and output data as soon as it's ready.

The exception at the front of a pipeline is **das_fetch**.  It takes no
stream input.  Give it a das catalog path and it walks the federated catalog,
works out the server URL and query string from the data source definition,
and streams the response body to standard output, whatever format that is.
Run `das_fetch node uiowa` to start browsing, or `das_fetch -h` for the
query key rules.

## Reporting bugs
Please use the issue tracker for the [das2C](https://github.com/das-developers/das2C/issues) 
github.com project to report any problems with the library.  If you've fixed a bug, 
1) thanks!, 2) please send a pull request so that your updates can be pulled into
the main project.

## Appreciation
Though most of the code in das2C was written by das developers, the vast majority
of the code in the files json.h and json.c in the das3 subdirectory is from 
[Neil Henning's](https://github.com/sheredom) elegant and efficient JSON parser, 
[json.h](https://github.com/sheredom/json.h).  Thanks Neil for sharing your
parser with the rest of the world, it's an example of classic C programming at
it's best.

In addition the code in toml.h and toml.c is from [CK Tan's](https://github.com/cktan) C99 comptabile [tomlc17](https://github.com/cktan/tomlc17) library.  Thanks CK Tan for supporting C99 software!

