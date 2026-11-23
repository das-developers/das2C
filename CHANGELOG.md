# das2C changes

das2C and its utility programs read, process and write das streams in C, 
Python, IDL and Javascript.  It provides no support for data display, as that
is handled by the extensive Java dasCore library, Autoplot and your own code
if desired.

Newest release first.


## 3.0.6 - 2026-11-22

Catalog names are now taken literally and paths start in one place, the
seed.  Callers of `DasCat_setPrefix()` need a small edit, see below.

### Behavior

- Catalog names are not interpreted: no `site:` or `test:` shorthands and
  no prefix.  With the default seed, the das catalog root, every path is a
  full name, `tag:das2.org,2012:site:/uiowa/juno`.

- A node's name does not include its own separator.  A seed or mount
  named with one on the end fails lookups with `DASMISS_BADNAME`.

- A name is under a node only when the node's separator follows it, so
  `tracers-10` is not under `tracers-1`.  A mount answers alone for the
  names under it.

- A catalog node whose separator is longer than 7 characters is unusable
  (`DASMISS_PARSE`).

- The compiled in GitHub mirror of the catalog root uses the `main`
  branch.

- A FlagSet flag with `subSep` is a multi-select, the das2 `set:`
  constraint.  Each switch on it sends its `pval`, and the elements are
  joined with `subSep` behind the flag's prefix once, in node order:
  `fan=090,270`, or `-lfdr LEx` with `subSep` "".

- An interface property with `"multi"` holds a list, `lfdr=Ex,Ew`.

- FlagSet entries in the protocol parameters can declair a 'subSep'
  to help handly tricky protocols that put to much structure in a 
  single get param

- Flags are now sent in the order the protocol section lists them

- The protocol section's `enum` lists are enforced, a string that is
  not listed will not parse to URL.  
  **Nodes with incomplete lists lose those choices** need to be 
  regenerated (older dasflex nodes: `cdf`, `csv`).

- A value joined with `subSep` is sent whole: when one control feeding
  it is off its default, the others send their defaults too.

- An empty value takes a setting back: `lfdr=` drops what an example
  set.

- Exclusive sets (`xorGroup`, mostly formats) are enforced.  The last
  member switched on wins and a setting of an unselected member is
  refused.  
  **Requests that mixed formats stop working**, e.g. `formats.csv
  das.version=das2`.

- A flag may have a `"suffix"`, sent after its value: `--win=2.5s`.

- Query URLs are easier to read: `dasurl_encode()` leaves `: / @ , =`
  plain, so times print as `2026-01-02T14:30`.  Servers decode either
  form the same way.

### Fixes

- `dasurl_encode()` used `isalnum()`, so in a single byte locale bytes
  above 127 went out unencoded.

- `dasarg_getOptVal()` could leave its buffer unterminated, and cut long
  values without saying so.  A value that does not fit is now an error.

- `dasarg_isOpt()` matched flags by prefix, so `--clean` matched
  `--cleanup`.

- A boolean control with a `pval` or `flag` and no `set.value` fired in
  both states.  It fires when switched on.

### API changes

- `DasCat_setSeed(NULL, uri)` seeds a node of the das catalog by name,
  found through the compiled in root and its mirrors.

- Removed: `DasCat_setPrefix()`, `DasCat_prefix()` and
  `DASCAT_SITE_PREFIX`.  Use `DasCat_setSeed()`.

- New: `DasCat_seedUri()`, `DASMISS_BADNAME`, `dasarg_optVal()`.

- `DasReq_set()` on a key already present moves the entry to the end,
  so entry order is the order things were last said.

- New: `DasSrcProp_multi()`.  `DasSrcProp_value()` gives a list default
  as comma separated items.

- `dasarg_getOptVal()` and `dasarg_isOpt()` accept `--opt=VALUE` and
  `--opt VALUE`, as GNU getopt does.

### Utilities

- das_fetch: paths start at `tag:das2.org,2012:site`, so
  `das_fetch node uiowa/juno` works.  Options were redone: `-r, --root`
  (full names), `-s, --seed` NAME, FILE or FILE=NAME (was `-r, --start`),
  `-o, --output` (was `-s`), `-t, --type` with short names such as das3,
  d2s, csv and ccsds (was `-a`), `-u, --update` (was `-f`), `--cache` and
  `--no-cache` (was `-C`), `--timeout` (was `-t`).  `-v` is the version,
  the log level defaults to info, and `-q` and verbose mode are gone.
  `DASFETCH_SEED` replaces `DASFETCH_START`.

- das_fetch: `url -o params` prints the source's base URLs, then one
  decoded query parameter per line, for reading.

- das_fetch: `KEY=a,b` sets a list property, and `node` shows list
  types and item count bounds.

### Build and test

- New TestArgs for the command line helpers.

- New fixture `test/das_fetch/subsep_0.7.json` for multi-select flags.

- New fixture `test/das_fetch/multi_0.7.json`, and TestSource cases for
  lists, flag order, protocol `enum` lists, exclusive sets and suffixes.

- The CMake and Windows das_fetch tests still used the removed `-C`
  option.

- The emscripten tests add TestAccum, TestXport, TestSource and TestOob,
  less the parts that need http.c, catalog.c, sockets or sub-processes.


## 3.0.5 - 2026-11-18

This release is source compatible for most callers.  The API changes below need small edits where those functions are used.  You'll need to recompile any apps. A re-link is not sufficient, the ABI changed slightly.

## Behavior
- Logins are sent only over https, and only to the server that asked.  A 401 over plain http fails without prompting.

- The library writes no files unless asked.  The catalog node disk cache is off by default. The `das_fetch` utilitiy turns it on.
  
- Cached catalog nodes honor `Cache-Control: no-store`.

- Reading an address (index) that arrays have not filled yet is not an error, instead 0, NULL or a null datum is returned.
  
- More errors are reported in the return value for programs that run in `DASERRDISP_RET` mode.

## API changes

- *changed*:`DasAry_valsIn()`, `DasAry_dataIn()` and the typed `*In` readers: count is `ptrdiff_t*`, negative on error.
  
- *changed*: `DasAry_qubeIn()` returns `ptrdiff_t`, negative on error.  Check any calls to DasAry_qubeIn in app code!

- *changed*: `DasBuf_prepare()`: space is `ptrdiff_t*`, negative on error.

- *changed*: `DasDs_codecFor()` returns int (1, 0, < 0), codec via out-param.

- *changed*: `DasDesc_valDouble()`, `valDatum()`, `valInt()` return int, value output is via a parameter.
  
- *changed*: `DasDesc_valStrs()` takes per-item buffers, like `DasProp_toStrs()`.

- *changed*: `DasCat_setCache()` is replaced by `DasCat_setCaching()`, `DasCat_caching()`, `DasCat_setCacheDir()` and `DasCat_setCacheAge()`.
  
- *Removed*: `DasDesc_valStrsSep()` and the `DasAry_floatAt()`-style macros in array.h.  These were unused, and unsafe for empty returns.
  
- *New*: `DASVAR_STR_SZ`.  `DasVar_toStr()` and `DasDim_toStr()` formats are documented and stable.

## Fixes

- Over-long time and datum strings could theoretically over run stack buffers. These now trigger an error return instead, and aren't truncated.  `DasDatum_fromStr`,  `DasTime_fromStr`
  
- Functions initialize their output params on every return path, including errors.  For example `DasAry_valsIn()` left `*pCount` stale.
  
- Deleting a dataset (`DasDs`) with exactly six codecs freed its inline codec array.
  
- das3 header write failures were not reported. `DasIO_writeDesc`

- `DasDesc_valDatum()` used the wrong units for time values.

- Catalog Enum FlagSet controls sent the flag name, not its value.

- A login was replayed to the target of a cross-origin redirect and a 401 at the redirect site would mark the good login refused.

## Utilities

- das_fetch: a `-C DIR` that is too long exits with code 2.

- das3_csv: now outputs blank cells where a value is missing.

## Build and test

- `make test_net` runs the network catalog test (TestCatalog, fixed), as before  `make test` stays offline.


## 3.0.4 - 2026-11-11

The first 3.0 release; changes from libdas2 2.3.

The library reads and writes das3 streams, which carry more structure than
das2 packets did.  Legacy das2 streams are upgraded internally so readers keep
working, but a program that builds its own data structures has catching up to
do.  The compiler will find most of it; what follows is what moved and why.

1. Values know what math applies to them.  A `DasVar` carries a `DasForm`,
   the C side of the stream's `<ops>` element: a free vector, a body centered
   position, a complex number, a rotation, plain numbers, or a point on a
   scale such as calendar time.  A formalism refuses operations that mean
   nothing, so adding two positions or scaling a date fails loud instead of
   returning a number.

2. Ask what a value can do, not what it is.  `DasForm_isKind()` replaces the
   per formalism identity tests and `DasForm_getParam()` reads parameters by
   name.  Code written that way keeps working when a formalism is added
   later.  Building a value is the exception and still names a concrete type.

3. One call reads components.  `das_datum_toDoubles()` handles any datum and
   `DasVar_compSym()` names each component.  The per formalism readers and
   symbol accessors are gone.

4. Component labels have a default.  `DasVar_compLabels()` labels any
   composite from a label property or, failing that, the dimension name, so
   an output format need not invent a scheme.  The compLabel property is no
   longer read; put one label per component in a stringArray label property
   instead.

5. Functions that could not report failure now can.  `das_datum_toDbl()` and
   others return a bool with an out parameter rather than handing back a fill
   value the caller could not tell from real data.

6. Coordinate symbols follow geodesy.  A geodetic position is (λ, φ, h),
   longitude first.  Planetographic longitude runs west, so that system is
   ordered latitude first and every component order in the library stays
   right handed.

7. Data sources can be found and queried through the das catalog.  A `DasCat`
   resolves catalog paths such as `uiowa/juno/wav/survey` to nodes, and can
   be seeded or mounted from local files to work offline.  A `DasSrc` reads a
   source's query interface and a `DasReq` holds what the client asks for, so
   `DasStreamSrc_url()` builds the URL to fetch without the program parsing
   catalog JSON itself.  Building the URL opens no socket, so the same code
   serves a command line tool or a browser build.  das_fetch is the command
   line front end.


## libdas2 1.2 - 2018-02-01

1. Many enhancements for consuming das2 streams.  Though libdas2 is not a
   graphic client (nor will it ever be) it can be used to read streams into
   memory arrays.  The new modules `array.c`, `builder.c` and `cordata.c`
   provide the new functionality.  Examples of reading local and remote
   streams may be found in `test/TestArray.c` and `test/TestBuilder.c`.

2. Libdas2 can now read from remote http servers.  The new modules
   `credmngr.c` and `http.c` provide this functionality.  In addition `io.c`
   (formerly `dasIO.c`) has been updated so that I/O read objects can be
   created for remote servers.

3. libdas2 bindings for D have been enhanced.

4. libdas2 now builds on MacOS X, as long as FFTW3 libraries are present.  At
   the time of this writing the easiest way to install it is to use the
   Homebrew package manager:

   ```
   brew install fftw
   ```


## libdas2 1.1

1. The Units class (UnitType) has been heavily updated to handle general SI
   unit parsing and conversion.  In the author's opinion this is the most
   general unit processing library short of using UDUNITS from unidata.  See
   `test/TestArray.c` for usage examples.

2. Support for storing stream data into continuous arrays has been added.
   Three classes, CorData (Correlated Datasets), DasAry (Multi-dimensional
   arbitrarily ragged arrays), and Builder (build CorData structures from
   Das2 Streams) provide support.  See `test/TestBuilder.c` for usage
   examples.

3. Stream download over HTTP has been added and the DasIO class has been
   updated to read over a network socket.

4. Power preserving discrete Fourier transform support has been added, see
   `dft.c` and `dft.h` and additions to `pywrap.c`.

5. An incomplete D language interface module has been started, comprised of
   the files `das2/package.d`, `das2/dwrap.d` and `daspkt.d`.

### Dependencies

- libz
- libexpat
- libfftw3 (new)
- urlparser (new)

### Developers

- Chris Piker
- Edward West


## libdas2 1.0

Libdas2 is a re-work of Jeremy's Das2StreamUtilities.  The library and its
utility programs read, process and write Das2 Streams.  It provides no
support for data display, as that is handled by the extensive Java dasCore
library and Autoplot.

### Changes from Das2StreamUtils

1. Much more attention is paid to memory usage.  This work-alike library
   should not leak memory.

2. Full Doxygen documentation has been added for all library functions.
   Documentation was written in Javadoc style for compatibility with the
   Netbeans IDE.

3. Utility programs have full manpage text.

4. Utility programs always return non-zero value to the shell on an error
   to better interoperate with the Das2 server.

5. Assumptions in the utility programs that packets contain only one `<y>`
   or `<yscan>` plane per packet have been removed.

6. timeXX and asciiXX data value encodings are now variable width instead of
   a fixed enumeration of sizes.

7. Error handling improved.  Library no longer calls `abort()` unless the end
   user wants it to, option to return on error instead of calling `exit()`
   added, error messages improved to provide specific file and line numbers
   for debugging.

8. `<z>` planes now allowed in packets, library checks for presence of
   corresponding `<y>` plane.

9. Encoding class introduced which allows for overriding I/O ASCII
   encodings.  Yscan items can also have a custom ASCII encoding.

10. Buffer class introduced to cut down on internal memory copies.

11. Callback processing improved.  User data pointers added to allow for
    stateful processing.  Both input and output operations can have
    processing hooks, 1-n callback sets allowed for input or output to allow
    for more modular processing.

12. Das2 Stream property handling updated.  Uses Standard property names
    from the Das 2.2 ICD, but is backwards compatible.  All standard property
    types are handled (boolean, int, double, String, Datum, and DatumRange).

13. C99 compliance.  GCC specific extensions have been replaced.

14. New library uses more standard C syntax.  `const` is used to indicate
    items which will not be mutated, array counters are of type `size_t` and
    pointers are visible as pointers and not hidden via typedefs.  All
    globals and static variables have been removed to allow for lib use in
    multi-threaded applications.

### Using libdas2 versus Das2StreamUtils

Linking: replace `-lDas2Stream` with `-ldas2`.

Compiling: replace `#include <Das2Stream.h>` with `#include <das2/core.h>`.

Scripting: program names have been changed so that they may exist in the
filesystem next to existing versions.

| Das2StreamUtils    | libdas2             |
|--------------------|---------------------|
| binAverage         | das2_bin_avg        |
| binAverageSeconds  | das2_bin_avgsec     |
| das1ToDas2         | das2_from_das1      |
| deflateStream      | das2_deflate        |
| inflateStream      | das2_inflate        |
| peakAverageSeconds | das2_bin_peakavgsec |
| toAscii            | das2_ascii          |
| toBinary           | das2_binary         |
| toSimpleAsciiTable | das2_table          |
