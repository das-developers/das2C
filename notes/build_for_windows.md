# Building das2C on Windows with Visual Studio tools

Windows software development is cumbersome.  Since there is no one accepted
standard for paths, tools and packages there are many different paths that
could be followed to build das2C for Windows.  The procedure given here is
not necessarily the best, just the path that the authors of das2C selected.

In short:

1. Install the Visual C++ build tools
2. Install vcpkg
3. Clone das2C and run `nmake`

All other dependencies, including CSPICE and CDF, are downloaded and built by
the makefile.


## 1. Install Visual Studio Tools

First we need a compiler.  Since most system DLLs were compiled with Visual C++,
this is the compiler we'll use.  To ensure compatibility with pre-compiled
Python3 modules we'll use:

> Microsoft Visual C++ 14.2 standalone

Go to <https://visualstudio.microsoft.com/downloads/>, open the
"Tools for Visual Studio 2019" accordion and find
"Build Tools for Visual Studio 2019".

Click "Download" to get the installer.  It should be a small file named
`vs_BuildTools.exe`.  Run this file as Administrator.

From the installer screen select "C++ build tools" then on the sidebar select
at least the following options:

* MSVC v142 - VS 2019 C++ x64/x86 build tools
* Windows 10 SDK

Select other options as you see fit.

In the "Install locations" tab, feel free to enter much shorter paths for the
install location.  The author shortened:

```
C:\Program Files(x86)\Microsoft Visual Studio\2019\BuildTools
C:\ProgramData\Microsoft\VisualStudio\Packages
C:\Program Files(x86)\Microsoft Visual Studio\Shared
```

to:

```
C:\opt\vs\2019\buildtools
C:\opt\vs\packages
C:\opt\vs\shared
```

The rest of these notes will assume the locations above.  Substitute as needed
for your system.

After install you will need to reboot your computer, hopefully it's not a server.


## 2. Install vcpkg

As of 2018, Microsoft has released a small dependency library handler tool
called vcpkg (Visual C++ Package).  It downloads code, not binaries so that you
can be sure that dependencies are built with the same version of the compiler as
the final project.  Multiple vcpkg clones can be placed on the same computer
without interfering with each other.

This is a welcome development.

To install vcpkg:

```bat
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat -disableMetrics
```

That's all.  The das2C makefile runs vcpkg itself, using the list of packages
in `vcpkg.json` at the top of the das2C source tree.  The packages are built the
first time you run nmake (this takes a while) and land in:

```
build.windows\vcpkg_installed\x64-windows-static
```

Visual Studio 2022 (17.6 and later) ships a copy of vcpkg and its developer
prompt sets `VCPKG_ROOT` for you, so with a recent VS 2022 this step may be
optional.


## 3. Checkout and build

For the next section we'll be using the 64-bit build tools.  First off, open a
command prompt (cmd.exe) and enter a 64-bit build environment using a command
similar to the following:

```bat
\opt\vs\2019\buildtools\VC\Auxiliary\Build\vcvars64.bat
```

This should output text similar to the following:

```
**********************************************************************
** Visual Studio 2019 Developer Command Prompt v16.11.19
** Copyright (c) 2021 Microsoft Corporation
**********************************************************************
[vcvarsall.bat] Environment initialized for: 'x64'
```

Note the `x64` printed at the bottom.

Next tell the makefile where you git cloned vcpkg:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
```

Then get the source, build and test the software:

```bat
git clone https://github.com/das-developers/das2C.git
cd das2C
nmake /f buildfiles\Windows.mak
nmake /f buildfiles\Windows.mak test
```

Install the programs and schemas, use `dev_install` to also install the static
library and headers needed to build against das2C.  Use `/n` to see what's going
to be installed first.

```bat
nmake /f buildfiles\Windows.mak PREFIX=C:\opt /n install
nmake /f buildfiles\Windows.mak PREFIX=C:\opt install
nmake /f buildfiles\Windows.mak PREFIX=C:\opt dev_install
```

This produces the static library `libdas3.lib`.  If you need a DLL, ask for it
by name, you'll get `das3.dll` and its import library `das3.lib`.  The second
command builds the DLL if needed and installs it, the import library and the
headers:

```bat
nmake /f buildfiles\Windows.mak dll
nmake /f buildfiles\Windows.mak PREFIX=C:\opt dll_install
```

To use dependency libraries you already have instead of the vcpkg ones (conda
does this), set both of the following and `VCPKG_ROOT` isn't needed:

```bat
set LIBRARY_INC=   :: directory containing expat.h, fftw3.h, openssl\, etc.
set LIBRARY_LIB=   :: directory containing the matching .lib files
```

The source file lists live in `buildfiles\sources.mak`, which is shared with
`buildfiles\Linux.mak`.  Add new source files there.


## 4. Building in SPICE and CDF tools

Add `SPICE=yes` and/or `CDF=yes` to any of the nmake commands above:

```bat
nmake /f buildfiles\Windows.mak SPICE=yes CDF=yes
nmake /f buildfiles\Windows.mak SPICE=yes CDF=yes test
nmake /f buildfiles\Windows.mak SPICE=yes CDF=yes PREFIX=C:\opt install
```

NAIF does not allow CSPICE to be hosted in a public git repository, and the
CDF library isn't in one either, so neither comes from vcpkg.  Instead the
makefile downloads the "PC, Windows, MS Visual C, 64bit" CSPICE toolkit from
NAIF and the 64-bit Windows CDF distribution from SPDF, and unpacks just the
headers and static libraries under `build.windows`.  This uses the `curl.exe`
and `tar.exe` that come with Windows 10 and up.

If you already have these, point the makefile at them instead:

```bat
set CSPICE_INC=C:\opt\cspice\include
set CSPICE_LIB=C:\opt\cspice\lib\cspice.lib

set CDF_INC=C:\opt\cdf\include
set CDF_LIB=C:\opt\cdf\lib\libcdf.lib
```

`nmake clean` keeps the downloaded and vcpkg built dependencies,
`nmake distclean` removes all of `build.windows`, dependencies included.
