# Building das2C for WebAssembly

`make EMSCRIPTEN=yes` cross-compiles `libdas3.a` to wasm32 with the
[emscripten](https://emscripten.org) toolchain.  Only the library and its
unit tests are built; the command line utilities are not, since a wasm
module runs inside some other program (a browser page, a node script) that
supplies its own I/O.  The first consumer is
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

The emsdk tools are Python programs needing 3.8 or newer.  On a host whose
default `python3` is older, point `EMSDK_PYTHON` at a newer interpreter;
note that `emsdk_env.sh` clears the variable, so export it after sourcing
as well as before.  The script does not put its node on `PATH` either; the
test target reads `EMSDK_NODE`, which it does export.  Built with emscripten
6.0 (clang 23) in 2026-10; anything recent should do.

## Expat

Emscripten ships a zlib port, used via `--use-port=zlib`, but no expat port,
so a libexpat compiled for wasm32 has to come from somewhere.  Pass its
header directory as `EXPAT_INC` for the build, and the archive as
`EXPAT_LIB` to link the tests.  This is the same shape as `CSPICE_INC` and
`CDF_INC` on the native builds: das2C never learns where the dependency
lives.

Two ways to get one.  libexpat's autotools build works under `emconfigure`
and `emmake`.  Or, since the parser is only three source files plus one
entropy source, compile them directly against a hand-written
`expat_config.h`; das2js does that from a pinned libexpat submodule with a
20-line config header, see its `buildfiles/expat_config.h` and `Makefile`.

## Build and test

```bash
$ make EMSCRIPTEN=yes EXPAT_INC=/path/to/expat/lib build
$ make EMSCRIPTEN=yes EXPAT_INC=/path/to/expat/lib EXPAT_LIB=/path/to/libexpat.a test
```

Output lands in `build.wasm32/`.  The archive is `libdas3.a`, 43 objects
in 2026-10.  Each test program is a `Test*.js` loader plus `Test*.wasm`,
run under node with the real filesystem mounted (`-sNODERAWFS`) so the
fixtures in `examples/` and `test/` are read in place.  `make test` is slow,
several minutes, almost all of it `TestFeed` re-feeding every fixture one
byte at a time; that is call overhead, not parsing speed.

`make EMSCRIPTEN=yes clean` removes `build.wasm32/` and needs neither emcc
nor `EXPAT_INC`.

## What is left out, and how to get it back

`buildfiles/Emscripten.mak` filters `buildfiles/sources.mak` rather than
keeping a second source list.  Its header comment is the authoritative
version of this table.

| left out                             | why                                   | switch               |
|--------------------------------------|---------------------------------------|----------------------|
| `http.c`, `credentials.c`, `node.c`  | sockets and OpenSSL; browsers do HTTP | none, see below      |
| `dft.c`                              | needs FFTW                            | `DFT=yes FFTW_INC=`  |
| `cli.c`, `send.c`                    | program scaffolding for utilities     | none                 |
| `spice.c`                            | needs CSPICE                          | none                 |

Two feature macros, `DAS_NO_NETWORK` and `DAS_NO_DFT`, are defined by
`Emscripten.mak` and nowhere else.  They compile the socket and SSL stream
modes out of `io.c` and `buffer.c` and the FFTW and HTTP setup out of
`util.c`.  No public header changes with them; a program compiled against
the installed headers sees the same API, and the socket constructors are
simply absent from the archive.

Network access from wasm is the embedding runtime's job (fetch in a browser,
http in node), feeding bytes to `DasIO_feed`.  The catalog walk in `node.c`
is likewise a JSON-and-HTTP exercise better done in the host language.

FFTW is expected back: spectrograms computed in C are a planned strength.
`DFT=yes` with `FFTW_INC` pointing at an FFTW built for wasm32 re-enables
`dft.c` today; nobody has built that FFTW yet.

## Platform notes

wasm32 is ILP32: `long` and `ptrdiff_t` are 4 bytes and `HOST_IS_64_BIT`
is undefined, so this is the first platform in years to exercise das2C's
32-bit paths.  `long double` is a 128-bit software float, which makes
libc's text-to-double conversion several times slower than on x86_64 (see
das2js `notes/performance.md`); binary streams are unaffected.  The libc is
musl, not glibc, and the compiler is clang with `-Werror`, so gcc-only
warning flags cannot go in `Emscripten.mak`.  Four long-standing bugs
turned up on the first build, none of them wasm-specific: a `false`
returned from a pointer function, float to int64 range checks that let a
value exactly on the limit overflow, an uninitialized leap-second table
when `CDF_LEAPSECONDSTABLE` is set, and `sscanf("%Lu")` for 64-bit
integers, which musl accepts and silently ignores.  Expect the same when
the next new platform arrives.

## Linking a program of your own

The archive is ordinary wasm32 object code.  Link it with emcc like any
static library, together with your expat archive and `--use-port=zlib`.
The test programs use

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
