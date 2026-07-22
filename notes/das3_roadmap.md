# das2C v3.0 roadmap -- what "done" means

STATUS: 2026-07-22.  

These are the items that remain before v3.0 is done. 

There is on particular order to the list, except item 1 should be done 
before the rest.  Items 2-7 are new or impoved utilities and item 10 will
take some restructuring of http.c and a new build target.

## Remaining items:

1. **das3_merge** (new, small).  Takes N readers on the command line, runs each,
   and emits a single MERGED stream synchronized on a specified coordinate
   variable.  Algorithm: emit the current MINIMUM value across the streams
   first, then proceed round-robin.  Driven by the Tracers Attitude work (DART
   project).

2. **das3_from_cdf** (finish; `utilities/das3_from_cdf.c` in flight).  A
   filename PATTERN (see `das2/uri.h`) maps a requested time range to
   filenames; the tool then extracts data from a pile of CDFs and emits a das3
   stream (or das2, where legal) over that time range.  Edge case: ISEE-1 Rapid
   Sample CDFs can't be passed through as-is (ISTP can't represent the
   structure, but command line hints may be able to make it work).

3. **das3_igrf** (finish; `utilities/das3_igrf.c` new).  Takes a SPICE
   metakernel and a body ID and produces the IGRF magnetic field at the body's
   location as a time-oriented stream.  May alternatively accept a das3 stream
   of location data as its input rather than computing the location itself.

4. **TLE location utility** (new).  Produces location data for any
   Earth-orbiting body from Two-Line Elements (TLEs).  Design notes:
   `co_notes/feature_TLE-ephemeris.md`.

5. **das3_bin_avgsec** (new).  The das3 equivalent of das2's `das2_bin_avgsec`.

6. **das3_cache_rdr** (new).  The das3 equivalent of `das2_cache_rdr`.  The
   toughest job of the lot.

7. **Multi-platform** . Make sure all new features build and test clean on
   Windows and MacOS

8. **Packaging** Setup github.com actions to automatically produce RPM, Deb
   an other packages as deamed appropriate.  Before the first packaged release,
   audit and prune the public symbol surface before a binary .so ships. After
   that we have to support the public API, right, wrong or indifferent.
   Also: `make install` currently leaves the schemas behind; install them to
   something like ${PREFIX}/share/das2C.

9. **emscripten build.**  Make `libdas3.a` build under emscripten, as the
   lead-in for dasView, the WASM-based das stream client.

## DASERR_NOTIMP inventory (the landmines)

Every `DASERR_NOTIMP` site in the library: "not yet implemented" stubs that
fail loud at runtime when a real stream hits them.  Line numbers drift;
re-grep to refresh:

    grep -rn "DASERR_NOTIMP" das2/ utilities/

13 sites as of 2026-07-21 (excluding the `defs.h` #define and the `util.h`
doc comment).

Ragged runs:
- `codec.c:602` -- interior fixed extent inside a ragged run structure (e.g.
  index="*;3;*"), caught by `DasCodec_raggedIndices`: run nesting assumes the
  external ragged indices sit contiguously after the record index, and
  per-level sub-run counting doesn't exist.
- `dataset.c:1046` + `codec.c:675` -- ZERO-LENGTH ragged run (tag count 0, or
  a terminator with nothing since the last close at its own level).  NOTIMP BY
  DESIGN: a 0-child element violates the ~9-year DasAry invariant "a sub-run
  has >= 1 element" (the lone exception, cubic-slice auto-fill, is a read-side
  view, not stored state).  Allowing it needs a full audit of DasAry / DasDs /
  DasDim / DasVar / iterator.c / builder.c and every das3 reader
  (das3_cdf/csv/spice) + das2py, so until a real stream needs it we fail loud.
  Probe: `test/notimp_zero_subrun.d3b` (holds both `[j|0]` and `[k|0]`).

Header:
- `dataset_hdr3.c` (values attr loop) -- the catch-all for unknown <values>
  attributes.  Not much of a gap anymore: valTerm works, ragged header values
  turned out to be incoherent (a values block's extents are fixed and declared
  by index=, so content lines up by count -- idxTerm was dropped from the
  schemas 2026-07-21), and repeat/repetitions are deferred to v3.1 (see
  Future below).

Properties:
- `property.c:827` -- time property conversion.

Variable algebra / encode:
- `var_bin.c:335`, `:399` -- binary operation not implemented.
- `var_bin.c:725`, `var_una.c:47` -- encoding scheme for unary operations.
- `var_con.c:268` -- encoding scheme for constants.

DFT:
- `dft.c:403` -- magnitude calculation for complex input.

IO:
- `io.c:653` -- defensive default arm of the read-mode switch (file / socket /
  SSL) with an abort(); unreachable unless a new stream mode is added.  A
  can't-happen guard, not a landmine.

CDF utility:
- `utilities/das3_cdf.c:2652` -- epoch conversion for type.

Priority notes: the value-algebra and DFT sites only bite specific operations;
`das3_cdf.c:2652` is the most likely to bite real data.  The ragged sites are
known design decisions, not surprises.

## Test coverage gaps

More coverage of the DASERR_DIS_RET error path is needed throughout the
library.  The default disposition is DASERR_DIS_EXIT, so under the normal test
harness an error just exits so any lines in a function AFTER a `das_error()` 
statement are rarely run.  Sweep for bugs by updating test programs to take
an error disposition argument.

## Future -- v3.1 and beyond

The following ideas are preserved here for future implementation or 
consideration in a v3.1 library and stream format.

- **Read DasCat: The Federated Catalog** There currently exists code to fetch a
  catalog node and a json parser, but no way to use it in remote queries. Would
  need to be created in conjunction with a Catalog JSON schema document.

- **Extension codecs ("codex", das2/codex.h).**  Real jpeg/png block decoders so
  ex28/ex29-class streams materialize instead of flattening; jpeg/png are `mime`
  values inside blob/base64.  This is the most likely item to be added.
  As soon as image dataset handling is needed, this is on the docket.

- **`<image>` element** -- only if real MULTI-CHANNEL (colorspace) imagery
  appears, it's not really needed for monochrome images.

- **das3_bin_avgsec image path** + the reduction-codec rule (never re-encode an
  average to jpeg; lossy-on-lossy).

- **`compression="gzip"` on `<packet>`** -- transport-layer block compression,
  dependency-free (zlib already linked).  Das2 supports full-stream compression
  that may be the way to go here as well, though compressing already compressed
  images would be tricky.

- **repeat/repetitions on `<values>` and `<sequence>`** -- repeating-ramp /
  repeated-value shorthands.  Since we now have `<sequence>'s` that are
  have one slope per index, this may never be needed.

- **Geotransform / 2-D offset grids** -- reference+offset composition beyond
  vector addition (see the v3.1 note).

- **Rotation Matrix / Quaternions** -- These are nice composite types that
  are fundamentally useful.  The resulting structures would include two
  frame references (from and to) as well as the required elements for the
  rotation. Non-rigid rotations may be considered.

## Notes

- Mission context: "Tracers" is the mission. "DART project" is the Tracers
  Attitude work that motivates das3_merge.

- das3_merge is the most externally-driven of the utilities.

- This is the strategic frame for the per-session `co_notes/handoff.md`; keep
  that file pointed here rather than duplicating the list.

-- Some text drafted by various Claude models and by the author of das2C.
