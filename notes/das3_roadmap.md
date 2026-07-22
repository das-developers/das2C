# das2C v3.0 roadmap -- what "done" means

STATUS: 2026-07-21.  The author's definition of a complete das2C v3.0.  When all
eight items below work, das3C is what he envisioned for Tracers: it handles every
Tracers need in the das2C domain that he is aware of.  The list is ordered as
given, not by strict priority -- though item 1 is the LIBRARY CORE (items 2-7 are
utilities that ride on the codec layer; item 8 will take some refactoring of
http.c and orthogonal build infrastructure).

## The eight

1. **Encode/decode every legal das3 stream.**  DECODE side COMPLETE as of
   2026-07-21: every `test/ex*` fixture parses except the two extension-codec
   ("codex") examples ex28/ex29, which are v3.1 scope by design and flatten to
   opaque bytes via `das3_text -f` (both golden-tested).  Multi-level ragged
   (`index="*;*;*"`) landed last, codec-internal: terminator-bounded (idxTerm)
   runs are codec business (`DasCodec_decodeRuns`/`_encodeRuns`), tag/frame
   bounded runs stay with the packet driver in dataset.c.  The ENCODE side has
   one legal-stream gap left: the `[idx|N]` count-tag writer (see the inventory
   below).  History and rationale: `co_notes/ragged_dev_plan.md`.

2. **das3_merge** (new, small).  Takes N readers on the command line, runs each,
   and emits a single MERGED stream synchronized on a specified coordinate
   variable.  Algorithm: emit the current MINIMUM value across the streams
   first, then proceed round-robin.  Driven by the Tracers Attitude work (DART
   project).

3. **das3_from_cdf** (finish; `utilities/das3_from_cdf.c` in flight).  A
   filename PATTERN (see `das2/uri.h`) maps a requested time range to
   filenames; the tool then extracts data from a pile of CDFs and emits a das3
   stream (or das2, where legal) over that time range.  Edge case: ISEE-1 Rapid
   Sample CDFs can't be passed through as-is (ISTP can't represent the
   structure, but command line hints may be able to make it work).

4. **das3_igrf** (finish; `utilities/das3_igrf.c` new).  Takes a SPICE
   metakernel and a body ID and produces the IGRF magnetic field at the body's
   location as a time-oriented stream.  May alternatively accept a das3 stream
   of location data as its input rather than computing the location itself.

5. **TLE location utility** (new).  Produces location data for any
   Earth-orbiting body from Two-Line Elements (TLEs).  Design notes:
   `co_notes/feature_TLE-ephemeris.md`.

6. **das3_bin_avgsec** (new).  The das3 equivalent of das2's `das2_bin_avgsec`.

7. **das3_cache_rdr** (new).  The das3 equivalent of `das2_cache_rdr`.  The
   toughest job of the lot.

8. **emscripten build.**  Make `libdas3.a` build under emscripten, as the
   lead-in for dasView, the WASM-based das stream client.

## DASERR_NOTIMP inventory (the landmines)

Every `DASERR_NOTIMP` site in the library: "not yet implemented" stubs that
fail loud at runtime when a real stream hits them.  Line numbers drift;
re-grep to refresh:

    grep -rn "DASERR_NOTIMP" das2/ utilities/

17 sites as of 2026-07-21 (excluding the `defs.h` #define and the `util.h`
doc comment).

Ragged runs:
- `dataset.c:1272` -- `[idx|N]` count-tag ENCODE for var-count NON-text runs:
  not-last always, and multi-level in LAST position (the packet frame cannot
  apportion bytes between levels).  The tag READ side (`_decode_ragged_run`)
  works; this writer is the one remaining item-1 encode gap.
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

Text / codec:
- `codec.c:1040` -- fixed text values larger than 127 bytes.

Sequences:
- `dataset_hdr3.c:769`, `:774` -- repeated sequence items (repeat= /
  repetitions=).

Header:
- `dataset_hdr3.c:1335` -- `<values>` repeat/repetitions/idxTerm attrs
  (valTerm honored as of 2026-07-08; blanket attr-rejection lifted).

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
`das3_cdf.c:2652` and `codec.c:1040` are the most likely to bite real data.
The ragged sites are known design decisions, not surprises.

## Test coverage gaps

More coverage of the DASERR_DIS_RET error path is needed throughout the
library.  The default disposition is DASERR_DIS_EXIT, so under the normal test
harness an error just exits and the lines AFTER a `das_error()` return never
run -- which is exactly where the 2026-07-16 use-after-frees lived (three XML
decoders read the freed parser to build the error message; harmless until a
caller sets DASERR_DIS_RET).  The fixes shipped with no regression test
because the corpus has no malformed-input fixture that reaches those paths.
Add malformed-header fixtures (and run at least some tests under DIS_RET) so
the post-error cleanup paths are actually exercised.

## Notes

- Mission context: "Tracers" is the mission.  "DART project" is the Tracers
  Attitude work that motivates das3_merge.
- das3_merge is the most externally-driven of the utilities.
- This is the strategic frame for the per-session `co_notes/handoff.md`; keep
  that file pointed here rather than duplicating the list.

-- drafted by Claude Opus 4.8 from the author's 2026-07-21 roadmap; merged with
   the NOTIMP inventory and updated by Claude Fable 5, filed by Chris Piker
