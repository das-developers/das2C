# Public API survey, 2026-10-25

Census of what das2C exports against who actually calls it, taken after the
pre-v3.0 API work on DasIO and the generator layer and before any of it was
committed.  Baseline is master 948724b.  The numbers come from
`tmp/api_survey.py`, which greps the installed headers (the `LIB_HDRS` list in
buildfiles/sources.mak) and cross references every name against the library's
own sources, the utilities, the tests, and the three downstream projects
checked out beside this repo: ../das2py, ../das2dlm and ../das2js.  Raw output
is in `tmp/api_survey_raw.txt`.  The tmp directory is not tracked, so the
script and its output live only in this checkout; rerun it with
`python3 -I tmp/api_survey.py` after the next round of changes to refresh the
counts.

Method limits worth knowing before reading the numbers:

- A "use" is a word-boundary match in a file, comments included.  It
  overcounts slightly and never undercounts.
- das2dlm still includes headers under the old `das2/` prefix, so it does not
  build against master.  Its numbers describe the v2.3 API it binds today,
  which is exactly what a migration has to carry.
- Struct field access is detected by `->field` tokens and attributed to every
  public struct that has a field of that name.  Common names (`units`, `type`,
  `value`, `pUser`) are ambiguous and the tables say so.

## 1. Summary statistics

### What this session changed

| | master 948724b | working tree |
|---|---|---|
| Installed headers | 49 | 48 |
| `DAS_API` functions | 695 | 697 |
| `DAS_API` variables (units constants and the like) | 68 | 68 |
| Public struct definitions | 77 | 70 |
| Struct-reaching accessor macros | 46 | 46 |

The function count did not shrink; it moved up a level.  Thirteen symbols left
the shared library: the five DasIO byte functions and the eight generator
functions.  Fifteen came in: `DasIO_writePktBytes`, `DasIO_getModel` as a
function, `DasIO_setOutVer`, `DasIO_getOutVer`, `DasIO_headerSent`, the five
`new_DasVar*` front doors, `DasVar_idxMap`, `DasStream_getVer`,
`DasStream_getType`, and the two enum toStr helpers.  Six shape and index
helpers relocated from generator.h to variable.h with their export intact.

The struct count fell by seven: DasIO and the six generator structs.

### Where the 697 exported functions stand, by widest caller

Each function is placed in the first category that applies.

| Category | Count | Meaning |
|---|---|---|
| downstream | 148 | named in das2py, das2dlm or das2js |
| utilities | 244 | named in utilities/ or examples/ but not downstream |
| tests only | 82 | named only in test/ |
| library internal | 214 | exported, but no caller outside das3/*.c |
| unused | 8 | no caller anywhere, library included |

So 392 functions have a caller outside the library and 304 do not.  Of the
304, the 82 tests-only entries are mostly API that is tested but not yet
consumed, and the 214 library-internal entries are the real trimming target,
with the caveat in section 4 that not all of them are plumbing.

### Downstream binding profile

Distinct exported functions each project names, and the headers it leans on.

| Project | Functions | Heaviest headers |
|---|---|---|
| das2py | 92 | dft.h 13, units.h 13, array.h 10, variable.h 8, io.h 7, node.h 5 |
| das2dlm | 62 | array.h 9, descriptor.h 7, buffer.h 6, dataset.h 5, io.h 5, units.h 5, util.h 5 |
| das2js | 55 | variable.h 9, array.h 8, json.h 6, property.h 6, io.h 5, builder.h 4 |
| utilities | 334 | |
| tests | 279 | |

All three downstream projects are engine clients: constructors, the builder,
`DasIO_readAll`, then array and variable access.  None of them touches the
das2 legacy layer (plane.h and packet.h have zero downstream callers) and none
touches uri.h, codec.h, oob.h, encoding.h or iterator.h.

## 2. Per-header function census

Total / downstream / utilities only / tests only / library internal / unused.

| Header | Total | Down | Util | Test | Lib | Unused |
|---|---|---|---|---|---|---|
| descriptor.h | 48 | 8 | 17 | 2 | 21 | 0 |
| array.h | 42 | 15 | 6 | 8 | 12 | 1 |
| variable.h | 40 | 16 | 11 | 7 | 6 | 0 |
| util.h | 39 | 5 | 11 | 3 | 20 | 0 |
| io.h | 37 | 10 | 15 | 1 | 10 | 1 |
| plane.h | 37 | 0 | 29 | 0 | 8 | 0 |
| dataset.h | 32 | 6 | 17 | 4 | 5 | 0 |
| value.h | 28 | 3 | 6 | 5 | 14 | 0 |
| stream.h | 27 | 3 | 12 | 2 | 10 | 0 |
| packet.h | 26 | 0 | 18 | 0 | 8 | 0 |
| buffer.h | 24 | 6 | 4 | 3 | 10 | 1 |
| property.h | 21 | 7 | 4 | 0 | 10 | 0 |
| time.h | 20 | 3 | 7 | 5 | 5 | 0 |
| datum.h | 19 | 1 | 12 | 5 | 1 | 0 |
| node.h | 18 | 5 | 4 | 1 | 4 | 4 |
| units.h | 18 | 13 | 2 | 3 | 0 | 0 |
| uri.h | 17 | 0 | 10 | 5 | 2 | 0 |
| dimension.h | 16 | 2 | 4 | 8 | 2 | 0 |
| codec.h | 15 | 0 | 5 | 0 | 10 | 0 |
| form.h | 14 | 3 | 2 | 4 | 5 | 0 |
| form_vec.h | 14 | 0 | 10 | 4 | 0 | 0 |
| dft.h | 14 | 13 | 0 | 0 | 1 | 0 |
| oob.h | 14 | 0 | 4 | 2 | 8 | 0 |
| encoding.h | 12 | 0 | 5 | 0 | 7 | 0 |
| credentials.h | 11 | 6 | 1 | 1 | 3 | 0 |
| json.h | 11 | 6 | 1 | 0 | 4 | 0 |
| iterator.h | 8 | 0 | 4 | 0 | 4 | 0 |
| form_cplx.h | 8 | 0 | 2 | 4 | 2 | 0 |
| http.h | 8 | 4 | 0 | 0 | 4 | 0 |
| log.h | 8 | 3 | 1 | 1 | 3 | 0 |
| form_pos.h | 7 | 0 | 5 | 0 | 2 | 0 |
| send.h | 7 | 0 | 5 | 1 | 1 | 0 |
| das1.h | 7 | 4 | 0 | 2 | 1 | 0 |
| builder.h | 6 | 4 | 0 | 1 | 0 | 1 |
| operator.h | 5 | 2 | 0 | 0 | 3 | 0 |
| form_rot.h | 4 | 0 | 0 | 0 | 4 | 0 |
| dsdf.h | 4 | 0 | 4 | 0 | 0 | 0 |
| processor.h | 3 | 0 | 3 | 0 | 0 | 0 |
| codex.h | 3 | 0 | 0 | 0 | 3 | 0 |
| defs.h | 2 | 0 | 1 | 0 | 1 | 0 |
| form_lin.h | 1 | 0 | 1 | 0 | 0 | 0 |
| form_pt.h | 1 | 0 | 1 | 0 | 0 | 0 |

## 3. Struct exposure

69 public struct definitions remain across 30 headers.  json.h holds ten,
value.h six, uri.h and form.h five each, variable.h and iterator.h four each.
The count matters less than who reaches in, so this section is organized by
reader.

### Utilities: 67 field names, 854 direct accesses

Dominated by the das2 legacy layer and the callback table:

- PlaneDesc and PktDesc: `sName` 114, `id` 113, `planeType` 22, `pData` 18,
  `planes` 11, `uPlanes` 9, `rFill` 5, `pEncoding` 3.  All from the das2
  utilities (das2_ascii, das2_bin_*, das2_psd and friends).
- StreamHandler: every callback slot is assigned by name, 60 accesses.  This
  is the struct's purpose; see the recommendation on a size field below.
- DasDim `aVars` 78 and `sDim` 7, DasDs `aDims` 7, `nRank` 19.  Iteration over
  a dimension's variables by hand even though `DasDim_numVars` and
  `DasDim_getVarByIdx` exist.
- `pUser` 79 across six structs.  By design.
- DasNode `nType` and `sPath`, DasEncoding `nWidth` and `nCat`, DasCodec
  `nBufValSz`, `sEncType`, `sSemantic`, DasUriIter `nRanges` and `bDone`.

### das2py: 31 field names, 72 accesses

- DasAry `sId`, `nRank`, `uFlags`, `units`; DasDs `lArrays`, `uArrays`,
  `lDims`, `uDims`, `sGroupId`; DasDim `aRoles`, `aVars`, `uVars`, `dtype`.
  The dataset-to-numpy walk reads the containers directly.
- das_error_msg `message`, `nErr`, `sFile`, `nLine`, `sFunc` for Python
  exception text.
- DasNode `pDom`, `sURL`, `sPath` for the catalog binding.
- json.h internals (`das_json_dict_el`, `das_json_str`, `das_json_num`) for
  catalog node contents.

### das2dlm: 39 field names, 180 accesses

- `type` 31 and `value` 24, almost all DasProp, with `units` 20 and `flags`
  2.  The IDL binding walks property records field by field.
- das_url `sHost`, `sPort`, `sPath`, `sQuery`, `sDataset` and DasHttpResp
  `url`, 33 accesses.  das2dlm picks apart URLs itself.
- das_credential `sServer`, `sRealm`, `sDataset` and DasCredMngr `pCreds`.
  It enumerates the credential store directly.
- DasDs `lDims`, `uDims`, `lArrays`, `uArrays`; DasDim `aRoles`, `aVars`,
  `uVars`; the same container walk das2py does.
- Iterator `shape` 10 (DasDsIter or DasAryIter).

### das2js: 16 field names, 94 accesses

- toml and json internals (`len`, `s`, `value`, `type`, `length`, `string`,
  `number`, `next`, `name`), 77 accesses.  The wasm glue reparses config and
  catalog text through the library's own parser structs.
- DasVarBin `pLeft`, `pRight`, `op`.  The one downstream reach into a
  variable's internals.
- OobExcept `sMsg`, OobComment `sType` and `sVal`, das_error_msg `message`.

### Struct-reaching accessor macros: 46

A macro that expands to `(P)->field` freezes that field at the tag whether or
not the struct is ever hidden, because the expansion lands in application
code.  The ones with any caller outside the library:

| Macro | Util | Test | py | dlm | js |
|---|---|---|---|---|---|
| DasDesc_type | 6 | 4 | 1 | 0 | 0 |
| DasDs_id | 5 | 1 | 0 | 1 | 1 |
| DasDs_group | 4 | 0 | 0 | 0 | 1 |
| DasDs_rank | 3 | 2 | 0 | 1 | 1 |
| DasDim_id | 4 | 3 | 0 | 2 | 1 |
| DasDim_numVars, _getVarByIdx, _getRoleByIdx | 4 each | 1 to 2 | 0 | 0 | 1 each |
| DasDim_dim, DasDim_type | 3 each | 0 | 0 | 0 | 1 each |
| DasDim_typeName, DasDim_getAxis | 0 to 1 | 0 | 0 | 0 | 1 each |
| DasForm_isKind, DasForm_kindStr | 1 each | 1 to 3 | 1 each | 0 | 1 each |
| DasProp_name | 2 | 0 | 1 | 0 | 1 |
| DasProp_units | 2 | 0 | 0 | 0 | 1 |
| DasProp_isRange, DasProp_isSet | 0 | 0 | 1 each | 0 | 0 |
| DasVar_units, DasVar_form | 2 to 3 | 2 | 0 to 1 | 0 | 1 each |
| DasVar_gen | 1 | 1 | 1 | 0 | 1 |
| DasAry_rank | 0 | 0 | 0 | 0 | 1 |
| DasDs_numCodecs, DasDs_getCodec, DasDs_pktItems, DasDs_getPlot | 2 each | 0 to 2 | 0 | 0 | 0 |
| DAS_FIELD_SZ | 5 | 0 | 0 | 0 | 0 |

Eighteen macros have no caller at all outside the library, among them all
five `DasCodex_*` accessors, `DasDs_mutable`, `DasEnc_isUtf8`,
`DasDim_hasAxes`, `DasDim_isPrimeCoord`, `DasStream_getEmbedAsBytes` and
`das_datum_valid`.

## 4. Recommendations

Ordered by leverage against risk.  Nothing here is urgent except the items
marked "before the tag", which become breaks afterward.

### 4.1 Before the tag: close the doors that can't be closed later

1. **StreamHandler needs a size or version field.**  It is the one struct
   every client fills in by hand, by design, and it has no way to grow.  A
   leading `size_t uSize` or `int nVersion` that the constructor checks lets
   a slot be added in 3.x without breaking a client compiled against 3.0.
   Same argument applies to `DasForm_VTbl` if third party formalisms are the
   expected extension point, which section 3 suggests they are.

2. **Convert the downstream-used accessor macros to functions.**  The table
   above names about eighteen.  Each is a one-line function and the change is
   source compatible.  After that the struct behind each can go opaque at any
   later time.  `DasVar_gen` should simply go: its only downstream use leaves
   when das2py adopts `DasVar_idxMap`.

3. **Make DasNode opaque**, as already decided on 2026-10-24.  das2py reads
   `pDom`, `sURL`, `sPath` and utilities read `nType` and `sPath`, so it
   needs four accessors first.

4. **Hide the http layer records**: das_url, DasHttpResp, das_credential and
   DasCredMngr.  das2dlm reaches into all four, but it reaches into the v2.3
   versions and has to be reworked for `das3/` anyway, so its migration is the
   moment to give it accessors instead.  The `new_DasIO_http` and
   `new_DasIO_url` constructors proposed in dasio_api_reduction.md remove most
   of the reason das2dlm and das2py touch these at all.

5. **DasVarBin should not expose `pLeft`, `pRight` and `op`.**  das2js reaches
   in.  variable.h already has `DasVarBin` as application facing for
   construction; two accessors for the operands and one for the operator
   cover das2js and let the struct body move to var_priv.h.

### 4.2 Un-export the plumbing

The 214 library-internal functions fall into two kinds and only one should be
trimmed.

**Plumbing**, meaning encode and decode steps, codec internals, buffer
init helpers and property record internals that exist because the library is
split across translation units.  These belong in `*_priv.h` headers on the
var_priv.h model.  Roughly 110 functions:

- descriptor.h: `DasDesc_encode2`, `_encode3`, `_encode3Bare`,
  `DasDesc_getTypeByIdx3`, `new_Descriptor` and the property-array setters.
  About 10 of its 21.
- codec.h: the 10 library-internal functions (`DasCodec_decode`,
  `_decodeRuns`, `_encode`, `_encodeRuns`, `_eatSpace`, `_postBlit`,
  `_isText`, `_isTrim`, `_setTrim`, `_deInit`) are the wire plumbing and can
  lose their export.  The header itself stays public: das3_text, das3_spice,
  das3_from_das2 and das3_from_cdf call `DasDs_addFixedCodec`,
  `DasCodec_update`, `DasCodec_init`, `DasCodec_setIdxTerms` and friends to
  declare how their output is encoded, and dataset.h includes codec.h because
  DasDs embeds `DasCodec aCodecs[DASDS_LOC_ENC_SZ]` by value (the small vector
  optimisation).  That embedding also means the DasCodec struct cannot go
  opaque without changing the DasDs layout, so it is frozen with DasDs at the
  tag.
- oob.h: all 8 encode and decode functions.
- packet.h and plane.h: `PktDesc_decodeData`, `_encodeData`, `_validate`,
  `_copyPlanes`, `new_PktDesc_xml`, `PlaneDesc_decodeData`, `_encode`,
  `_encodeData`, `new_PlaneDesc_empty`.
- stream.h: `DasDesc_decode`, `DasStream_encode2`, `_encode3`,
  `new_DasStream_str`, `DasStream_clonePktDesc`, `_rmDesc`.
- encoding.h: `DasEnc_read`, `_equals`, `_hash`, `new_DasEncoding_str`.
- property.h: `DasProp_init`, `_size`, `_escapeSize`, `_xmlValue`,
  `_invalidate`, `dasprop_memsz`, `dasprop_unescapeSep`.
- buffer.h: `DasBuf_initReadOnly`, `_initReadWrite`, `_wrapWrite`,
  `_writeFrom`, `_writeSpace`, `_readOffset`, `_strip`.
- io.h: `DasIO_queryExcept`, `DasIO_serverExcept`, `DasIO_sendLog`,
  `DasIO_closeNoData`, `new_DasIO_str`.  Utilities should be using send.h
  for these.
- util.h: `das_errdisp_get_lock`, `_release_lock`, `das_error_func`,
  `das_error_func_fixed`, `das_store_str`, `das_tokncpy`, `das_memset`.
- codex.h: all three functions and five macros have no caller outside the
  library today, but DasCodex is the planned extension point for application
  supplied codecs, so it stays public.  Worth the same treatment as the other
  extension tables: a size or version field in the context struct before the
  tag, and the five accessor macros as functions.
- iterator.h: the four `_init` and `_next` pairs are the implementation
  behind the public iterator macros.

**Genuine API with no caller yet**, which should stay exported.  Examples:
`DasDesc_getBool`, `DasDesc_getDouble`, `DasDesc_setBool`, the `dt_*` time
helpers, `das_lib_version`, `das_version`, `das_b64_decode`, `DasIO_getModel`,
`DasIO_getOutVer`, `DasIO_setLogLvl`, `DasIO_setTaskSize`, `DasAry_setFill`,
`DasAry_setId`, `DasDs_setMutable`, `DasDim_degenerate`, `DasVar_idxMap`,
`new_DasVarCompSeq`.  Around 100 functions.  The test is whether a
reasonable application would ever call it; the plumbing above fails that test.

### 4.3 Delete the eight unused exports

`DasAry_cmp`, `DasBuf_paragraph`, `DasIO_getLogLvl`, `DasNode_isSpaseRec`,
`DasNode_isSpdfCat`, `DasNode_isStreamSrc`, `DasNode_type`,
`stream_from_stdin`.  All eight are defined, so each is a real symbol in the
archive with no caller.  `stream_from_stdin` in builder.c is a convenience
nothing adopted.  The four DasNode predicates may be worth keeping as part of
the node.h review on feature/fetch.

### 4.4 The das2 legacy layer

plane.h and packet.h are 63 exported functions and about 230 direct field
accesses, all from the das2 utilities and none from downstream.  The das2
format itself is frozen, so freezing these structs costs little, and
converting fourteen utilities to accessors buys nothing anyone has asked for.
Recommendation: leave them public and open, document them as the legacy
layer, and spend the effort on the das3 side.

### 4.5 Give the container walks a path that isn't the struct

das2py, das2dlm and several utilities iterate `DasDs.lArrays`, `DasDs.lDims`,
`DasDim.aVars` and `DasDim.aRoles` by hand.  Accessor macros already exist for
most of this (`DasDs_numAry`, `DasDs_getAry`, `DasDim_numVars`,
`DasDim_getVarByIdx`, `DasDim_getRoleByIdx`), they just are not used.  Turn
them into functions (4.1 item 2), add the missing dimension iterator for
`DasDs`, and then the three container structs can go opaque in a later minor
release without touching anyone who has moved over.

### 4.6 Tests-only functions

82 functions, concentrated in dimension.h (8), array.h (8), variable.h (7),
value.h, uri.h, time.h and datum.h (5 each), and the formalism headers.
Most are API that exists because the design called for it and the tests are
the first consumer.  No action, but the form_cplx.h and form_vec.h entries
are worth a glance: a formalism function that only a test calls may be an
extension hook nobody has needed.

## 5. What a trimmed 3.0 would look like

Applying 4.1 through 4.3 and the private-header moves in 4.2:

| | now | after |
|---|---|---|
| Installed headers | 48 | 48 |
| `DAS_API` functions | 697 | roughly 590 |
| Public struct definitions | 70 | roughly 60 |
| Struct-reaching macros | 46 | roughly 20, all on legacy or callback structs |

The remaining exposure would be the das2 legacy layer (by choice), the
callback and extension tables StreamHandler, DasForm_VTbl and DasCodex (by
design, versioned), DasCodec because DasDs embeds it by value, the json and
toml parser records that das2js and das2py read (candidates for a later
pass), and the plain data records like das_error_msg and das_time that are
records on purpose.

## 6. How far opacity could go

The 69 public structs sorted by what an application does with the type rather
than by header.  The first group is the DasIO move repeated thirty times; the
other three are not candidates for the same treatment.

| Kind | Count | Examples | Opaque? |
|---|---|---|---|
| Heap objects behind constructors | about 30 | DasDs, DasDim, DasAry, DasStream, DasNode, PktDesc, PlaneDesc, DasDsBldr, DasUriTplt, the four DasVar structs, DasForm, DasCredMngr, Das2Dft, Das2Psd | Yes, with accessors.  Most of the accessors already exist as macros; see section 3. |
| Stack value types in hot paths | about 20 | das_time, das_datum, DasBuf, OobExcept, OobComment, das_range, das_error_msg, das_operand, the four iterators, the six small pairs in value.h | Only at a cost.  Declare them stable records instead. |
| Tables the application fills by value | 3 or 4 | StreamHandler, DasForm_VTbl, DasBinOp_VTbl, DasCodex in part | No, they are the contract.  Version them (section 4.1). |
| Vendored parser records | 13 | the ten das_json structs, the three dastoml structs | Not worth it, they are another library's shape. |

Notes on each group:

- **Heap objects.**  Opaque pointers keep the first-member cast the library
  leans on, since `(DasDesc*)pDs` is a pointer cast and compiles against an
  incomplete type.  The library only has to keep the base as the first member
  internally, which it does.  DasCodec is the exception: it sits inside DasDs
  by value, so it goes opaque only together with DasDs.
- **Stack value types.**  A das_datum is produced for every element a reader
  pulls through `DasVar_get()`, an iterator is declared per loop and das_time
  is passed by value everywhere.  C offers two ways to hide those layouts and
  both cost something.  Heap allocating them puts a malloc in the inner loop.
  The pthread_mutex_t trick, a sized aligned byte blob with the real struct in
  the .c and a compile-time size check, hides the layout but freezes the size,
  loses debugger field names and turns every field read into a function call,
  since C99 cannot inline across translation units without exposing the body.
  OpenSSL lived with that for years and then moved to the heap anyway.  For
  this library these are records on purpose, like `struct tm`, and the right
  move is to say so in the header and freeze them deliberately.  das_time
  already works that way.
- **Tables.**  See 4.1 item 1.
- **Vendored.**  json.h and toml.h expose the records of the parsers they
  wrap.  Wrapping them again is writing an API over an API for no client who
  asked.

Applied in full: about 30 structs become handles, 20 are declared stable
records, 4 are versioned tables and 13 stay as they are.  That is 45 of 69
with a hidden or deliberately frozen layout, and the remaining exposure is
entirely things an application declares on its own stack.  Which of the 30
to convert before the tag versus in a 3.x minor depends on how many are
expected to be reshaped.  DasIO and DasGen were the two with a concrete reason.
The field-access tables in section 3 name DasNode, the http records and
DasVarBin as the next three with a known client reaching in.

### Naming follows the split, with a few strays

Sorting by kind also shows the house convention almost holds: stack value
types are snake_case (das_time, das_datum, das_range, das_error_msg,
das_operand, das_byte_seq) and heap objects are CamelCase (DasDs, DasAry,
DasStream).  Where the typedef name differs from the struct tag, as in
`struct das_var` for DasVar or `struct das_form` for DasForm, the tag is
snake_case and the typedef CamelCase, which is consistent with the rule since
the typedef is what applications spell.

The strays are stack types with CamelCase names, several of them older than
the convention:

- StreamHandler, the oldest, predating the current maintainer.  A callback
  table filled by value; `das_stream_handler` or similar would fit the rule.
- OobExcept, OobComment and OutOfBand: declared on the stack by every utility
  that raises an exception.
- The four iterators DasDsIter, DasDsUniqIter, DasDsCubeIter and DasAryIter,
  plus DasUriIter: stack objects by design, for the streaming discipline.
- DasBuf, DasHttpResp, DasEncoding, DasProp: used both ways, which is part of
  why their naming is ambiguous.
- DasForm_VTbl and DasBinOp_VTbl: tables, filled by value.

Renaming any of these is a source break, so it is a before-the-tag item or
never.  das2_compat.h already carries aliases of exactly this kind
(StreamDesc for DasStream), so the mechanism for a soft landing exists.
