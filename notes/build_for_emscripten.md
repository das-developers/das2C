# Building das2C for WebAssembly

`make -f buildfiles/emscripten.mak` cross-compiles `libdas3.a` to wasm32
with the [emscripten](https://emscripten.org) toolchain.  Like
`buildfiles/windows.mak` it is its own entry point; the top level
`Makefile` never reads it.  Only the library and its unit tests are built;
the command line utilities are not, since a wasm module runs inside some
other program (a browser page, a node script) that supplies its own I/O.
The first consumer is
[das2js](https://github.com/das-developers/das2js), which wraps the archive
in a flat C glue layer and an ES module.  Nothing in das2C knows about that
layer: no export markup, no emscripten headers beyond the byte-order probe
in `das3/defs.h`.

## Toolchain

Install and activate the emsdk, then source its environment script in the
shell that will run make:

```bash
$ git clone https://github.com/emscripten-core/emsdk.git
$ cd emsdk && ./emsdk install latest && ./emsdk activate latest
$ source ./emsdk_env.sh
```

The emsdk tools are Python programs needing 3.10 or newer.  On a host whose
default `python3` is older, point `EMSDK_PYTHON` at a newer interpreter;
note that `emsdk_env.sh` clears the variable, so export it after sourcing
as well as before.  The script does not put its node on `PATH` either; the
test target reads `EMSDK_NODE`, which it does export.  Last built with
emscripten 6.0.13 in 2026-10, the version `.github/workflows/emscripten.yml`
pins; anything recent should do.

## Dependencies

Emscripten ships a zlib port, used via `--use-port=zlib`, but no expat or
FFTW.  The first build downloads both release tarballs, checks each against
a pinned SHA-256, and configures, builds and installs them for wasm32 under
`deps.wasm32/`, the way the native builds fetch CDF and CSPICE.  So the first
build needs network access; later ones reuse `deps.wasm32/` until a
`distclean`.  If a download site is down, the `EXPAT_URL` and `FFTW_URL`
comments in `buildfiles/emscripten.mak` give a mirror.

To use copies already built for wasm32 instead, set both variables of a
pair:

| variables                | point at                                       |
|--------------------------|------------------------------------------------|
| `EXPAT_INC`, `EXPAT_LIB` | the directory holding `expat.h`, `libexpat.a`  |
| `FFTW_INC`, `FFTW_LIB`   | the directory holding `fftw3.h`, `libfftw3.a`  |

FFTW is GPL, see `THIRD-PARTY.txt`.  It is in `libdas3.a` through `dft.c`,
but a linker only carries it into a program that calls a transform.

## Build and test

```bash
$ make -f buildfiles/emscripten.mak build
$ make -f buildfiles/emscripten.mak test
```

Run these from the top of the source tree.  Output lands in
`build.wasm32/`.  The archive is `libdas3.a`, 47 objects in 2026-10.  Each
test program is a `Test*.js` loader plus `Test*.wasm`, run under node with
the real filesystem mounted (`-sNODERAWFS`) so the fixtures in `examples/`
and `test/` are read in place.  `make test` is slow, several minutes, almost
all of it `TestFeed` re-feeding every fixture one byte at a time; that is
call overhead, not parsing speed.  `build_test` links the test programs
without running them.

`clean` removes `build.wasm32/`; `distclean` also removes `deps.wasm32/`,
the expat and FFTW downloads and builds.  Neither needs emcc.

## What is left out, and how to get it back

`buildfiles/emscripten.mak` filters `buildfiles/sources.mak` rather than
keeping a second source list.  Its header comment is the authoritative
version of this table.

| left out                                | why                                   | switch   |
|-----------------------------------------|---------------------------------------|----------|
| `http.c`, `credentials.c`, `catalog.c`  | sockets and OpenSSL; browsers do HTTP | none     |
| `cli.c`, `send.c`                       | program scaffolding for utilities     | none     |
| `dft.c`                                 | in by default                         | `DFT=no` |

`spice.h` has no library code behind it, so there's nothing to leave out for
SPICE.  A program that includes it needs CSPICE, which isn't built for wasm.

Two feature macros compile code out.  `DAS_NO_NETWORK` is defined by
`das3/defs.h` whenever `__EMSCRIPTEN__` is, since no wasm build has
sockets; it removes the socket and SSL stream modes from `io.c` and
`buffer.c` and the HTTP setup from `util.c`.  `DAS_NO_DFT` is defined by
`DFT=no` and removes the FFTW setup from `util.c`.  No public header changes
with either; a program compiled against the installed headers sees the same
API, and the left out functions are simply absent from the archive.

Network access from wasm is the embedding runtime's job (fetch in a browser,
http in node), feeding bytes to `DasIO_feed`.  The catalog walk stays:
`node.c`, `source.c` and `req.c` never touch a transport, so the host side
fetches each node and `DasNodeWalk` does the rest.

## FFTW on wasm

FFTW is built with its generic SIMD128 codelets, which clang lowers to wasm
SIMD; every current browser and node runs it.  If a build or a runtime
fails over it, the comment above the FFTW rule in `emscripten.mak` says what
to drop.

Plans are made with `FFTW_ESTIMATE`, not the `FFTW_MEASURE` native builds
use, and no wisdom file is read.  There is nothing to time with on wasm:
the tick counter FFTW's configure finds there never advances, so measured
planning would never return.  `emscripten.mak` configures FFTW without it
and `dft.c` asks for estimates regardless, which covers an FFTW supplied
through `FFTW_LIB`.  Wisdom is specific to the machine and build that made
it, so a native wisdom file would be of no use here anyway.

## Platform notes

wasm32 is ILP32: `long` and `ptrdiff_t` are 4 bytes and `HOST_IS_64_BIT`
is undefined, so this is the first platform in years to exercise das2C's
32-bit paths.  `long double` is a 128-bit software float, which makes
libc's text-to-double conversion several times slower than on x86_64 (see
das2js `notes/performance.md`); binary streams are unaffected.  The libc is
musl, not glibc, and the compiler is clang with `-Werror`, so gcc-only
warning flags cannot go in `emscripten.mak`.  Four long-standing bugs
turned up on the first build, none of them wasm-specific: a `false`
returned from a pointer function, float to int64 range checks that let a
value exactly on the limit overflow, an uninitialized leap-second table
when `CDF_LEAPSECONDSTABLE` is set, and `sscanf("%Lu")` for 64-bit
integers, which musl accepts and silently ignores.  Expect the same when
the next new platform arrives.

## Linking a program of your own

The archive is ordinary wasm32 object code.  Link it with emcc like any
static library, followed by the dependency archives,
`deps.wasm32/fftw/lib/libfftw3.a` and `deps.wasm32/expat/lib/libexpat.a`
(or your own), and `--use-port=zlib`.  The test programs use

```
-sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
```

A browser module instead wants `-sMODULARIZE`, an `EXPORTED_FUNCTIONS`
list naming the C entry points it calls, and no filesystem; das2js is the
worked example.  The default emscripten stack is 64 KiB, and das2C keeps
some large automatics (packet buffers, `das_datum` arrays), hence the
1 MiB stack above.

To profile, add `--profiling-funcs` to the link so the wasm keeps function
names, then `node --cpu-prof Test.js` and open the `.cpuprofile` in any
Chromium-family devtools or `speedscope`.
