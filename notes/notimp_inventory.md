# DASERR_NOTIMP landmine inventory

STATUS: 2026-06-28.  Snapshot of every `DASERR_NOTIMP` site in das2C, taken while
implementing bool text encoding (the real TRACERS CDPU status stream tripped one).
These are "not yet implemented" stubs that fail at runtime when a real stream hits
them -- landmines, not dead code.  Re-grep to refresh:

    grep -rn "DASERR_NOTIMP" das2/ utilities/

18 real sites (excluding the `defs.h` #define and the `util.h` doc comment).

## Being cleared now (this session)

- `codec.c:301`  -- "TODO: Add parsing for 'true', 'false' etc."  (bool text codec)
- `property.c:775` -- "Boolean property conversion not yet implemented" (DasProp_convertBool)

Both real: the TRACERS CDPU status stream (ex21) carries `semantic="bool"` data,
so das2C is overdue here.  One shared `das_str2bool` helper feeds both.

## Still laying about (by area)

Text / codec:
- `codec.c:708`  -- fixed text values larger than 127 bytes not implemented

Properties:
- `property.c:799` -- time property conversion not implemented

Ragged decode (the ex27-33 work; expected, these gate the ragged dev plan):
- `dataset.c:982`  -- ragged non-text array not at end of packet
- `dataset.c:1064` -- ragged non-text array not at end of packet

Sequences:
- `dataset_hdr3.c:629`  -- sequences not supported for vectors
- `dataset_hdr3.c:641`  -- repeated sequence items (repeat=)
- `dataset_hdr3.c:646`  -- repeated sequence items (repetitions=)

Header:
- `dataset_hdr3.c:1014` -- attributes of <values> element not supported

Variable algebra / encode:
- `var_bin.c:324`, `:388`, `:710` -- binary operation not implemented
- `var_con.c:251` -- encoding scheme for constants not implemented
- `var_una.c:47`  -- encoding scheme for unary operations not implemented

DFT:
- `dft.c:403` -- magnitude calculation for complex input not implemented

IO:
- `io.c:649` -- "not implemented" (check context)

CDF utility:
- `utilities/das3_cdf.c:2652` -- epoch conversion for type not implemented

## Notes

- The ragged + sequence-vector + repeat ones are KNOWN gaps tracked in
  ragged_dev_plan.md / multi-index sequence work, not surprises.
- The var_bin/var_con/var_una/dft ones are in the value-algebra layer and only
  bite specific operations; lower priority until a stream needs them.
- `das3_cdf.c:2652` and `codec.c:708` are the next most likely to bite real data.
