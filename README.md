# libdas2, version 2.2_stable
das2 stream reading/writing library for C and Python.  

This version is not compatable with Python 3.  It mostly exists to support
legacy software.

libdas2 is a re-worked version of Das2StreamUtils with the addition of
das1 support and more wrappers to support server-side stream processesing.
It intended for reading, processing and writing Das2 Streams.  It provides
no support for data display as that is handled by the extensive Java dasCore
library and Autoplot.  

To build this version of the software issue:
```bash
$ export PYVER=2.7

$ export N_ARCH=/                       # or if shared NFS use an OS name such as mint18
                                        # this is just a path string, so any one word is okay
													 
$ export PREFIX=/your/install/location  # /usr/local works.
$ make
$ make -n inst
```

To clean up the build area and try new settings issue:
```bash
$ make distclean
```
and then you can set new environment variables.


