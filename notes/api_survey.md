# Public API survey

Census of what das2C exports against who calls it.  The counts below are for
master 75aff62 plus the uncommitted DFT, DasEnc, naming and version work,
taken 2026-11-03.

The numbers come from `tmp/api_survey.py`, which reads the installed headers
(the `LIB_HDRS` list in buildfiles/sources.mak) and cross references every
name against the library's own sources and private headers, the utilities,
the tests, the examples, and the three downstream projects checked out beside
this repo: ../das2py, ../das2dlm and ../das2js.  Raw output is in
`tmp/api_survey_raw.txt`.  The tmp directory is not tracked, so the script
lives only in this checkout; rerun it with `python3 -I tmp/api_survey.py`
after the next round of changes.

Method limits worth knowing before reading the numbers:

- A "use" is a word-boundary match in a file, comments included.  It
  overcounts slightly and never undercounts.
- The downstream checkouts are das2py ef21bb3 (2026-10-19), das2js a0b81d6
  (2026-10-30) and das2dlm 11d8b3c (2025-07-28).  das2dlm still includes
  headers under the old `das2/` prefix and does not build against master; its
  numbers describe the v2.3 API it binds, which is what its migration has to
  carry.
- Struct field access is detected by `->field` tokens and attributed to every
  public struct with a field of that name.  Common names (`units`, `type`,
  `value`, `nType`, `sPath`) are ambiguous, and a token that matches only a
  private struct is not counted at all.  Section 3 corrects for both by hand.

## 1. Summary

| | |
|---|---|
| Installed headers | 50 |
| Exported symbols (`nm -D` on libdas3.so) | 931 |
| `DAS_API` functions | 854 |
| Public struct definitions | 43 |
| Struct-reaching accessor macros | 7 |

The function count is high because the opaque passes turned accessor macros
into real functions.  That is the intended trade: each one is a symbol that
survives a layout change.

### Exported functions by widest caller

Each function is placed in the first category that applies.

| Category | Count | Meaning |
|---|---|---|
| downstream | 195 | named in das2py, das2dlm or das2js |
| utilities | 318 | named in utilities/ or examples/ but not downstream |
| tests only | 127 | named only in test/ |
| library internal | 211 | exported, but no caller outside das3/ |
| unused | 3 | no caller anywhere, library included |

### Downstream binding profile

Distinct exported functions each project names, and the headers it leans on.

| Project | Functions | Heaviest headers |
|---|---|---|
| das2js | 131 | node.h 27, var.h 14, array.h 12, source.h 11, dim.h 10 |
| das2py | 74 | units.h 13, array.h 10, var.h 9, io.h 7, prop.h 6 |
| das2dlm | 57 | array.h 9, desc.h 7, buffer.h 6, datset.h 6, io.h 5 |
| utilities | 444 | |
| tests | 427 | |

das2py and das2dlm still call the v2.3 names of the renamed dft.h and
creds.h functions, which these counts do not match; section 3 lists them.

All three downstream projects are engine clients: constructors, the stream
reader, then array and variable access.  das2js also drives the catalog
(node.h, source.h).  None of them touches the das2 legacy layer (plane.h and
packet.h) or uri.h, codec.h, oob.h, enc.h or iter.h.

## 2. Per-header function census

Total / downstream / utilities only / tests only / library internal / unused.

| Header | Total | Down | Util | Test | Lib | Unused |
|---|---|---|---|---|---|---|
| source.h | 60 | 11 | 28 | 18 | 3 | 0 |
| desc.h | 50 | 10 | 19 | 1 | 20 | 0 |
| var.h | 46 | 19 | 10 | 8 | 9 | 0 |
| util.h | 44 | 5 | 17 | 5 | 17 | 0 |
| datset.h | 41 | 10 | 21 | 5 | 5 | 0 |
| io.h | 41 | 11 | 14 | 2 | 13 | 1 |
| array.h | 40 | 18 | 4 | 8 | 9 | 1 |
| plane.h | 40 | 0 | 30 | 0 | 10 | 0 |
| node.h | 33 | 28 | 3 | 0 | 2 | 0 |
| stream.h | 30 | 2 | 14 | 2 | 12 | 0 |
| value.h | 29 | 5 | 6 | 5 | 13 | 0 |
| req.h | 27 | 2 | 6 | 18 | 1 | 0 |
| dim.h | 25 | 10 | 5 | 8 | 2 | 0 |
| buffer.h | 24 | 6 | 4 | 3 | 10 | 1 |
| packet.h | 24 | 0 | 17 | 0 | 7 | 0 |
| prop.h | 22 | 11 | 5 | 0 | 6 | 0 |
| catalog.h | 21 | 0 | 17 | 3 | 1 | 0 |
| time.h | 21 | 4 | 6 | 6 | 5 | 0 |
| datum.h | 19 | 3 | 11 | 5 | 0 | 0 |
| units.h | 18 | 13 | 2 | 3 | 0 | 0 |
| uri.h | 17 | 0 | 10 | 5 | 2 | 0 |
| dft.h | 16 | 2 | 3 | 0 | 11 | 0 |
| enc.h | 14 | 0 | 7 | 0 | 7 | 0 |
| form_vec.h | 14 | 0 | 10 | 4 | 0 | 0 |
| oob.h | 14 | 0 | 4 | 2 | 8 | 0 |
| form.h | 13 | 3 | 3 | 4 | 3 | 0 |
| http.h | 12 | 4 | 2 | 2 | 4 | 0 |
| creds.h | 11 | 1 | 3 | 3 | 4 | 0 |
| json.h | 11 | 7 | 1 | 0 | 3 | 0 |
| codec.h | 9 | 0 | 9 | 0 | 0 | 0 |
| iter.h | 8 | 0 | 4 | 0 | 4 | 0 |
| form_cplx.h | 8 | 0 | 2 | 4 | 2 | 0 |
| log.h | 8 | 3 | 2 | 1 | 2 | 0 |
| form_pos.h | 7 | 0 | 5 | 0 | 2 | 0 |
| send.h | 7 | 0 | 5 | 1 | 1 | 0 |
| das1.h | 7 | 4 | 0 | 1 | 2 | 0 |
| oper.h | 5 | 2 | 0 | 0 | 3 | 0 |
| form_rot.h | 4 | 0 | 0 | 0 | 4 | 0 |
| dsdf.h | 4 | 0 | 4 | 0 | 0 | 0 |
| proc.h | 3 | 1 | 2 | 0 | 0 | 0 |
| codex.h | 3 | 0 | 0 | 0 | 3 | 0 |
| defs.h | 2 | 0 | 1 | 0 | 1 | 0 |
| form_lin.h | 1 | 0 | 1 | 0 | 0 | 0 |
| form_pt.h | 1 | 0 | 1 | 0 | 0 | 0 |
|  |  |  |  |  |  |  |

## 3. Struct exposure

43 public struct definitions, sorted by what an application does with the
type:

| Kind | Count | Structs |
|---|---|---|
| Heap objects behind constructors | 2 | DasUriTplt, DasCredMngr |
| Stack records | 22 | das_time, das_datum, DasBuf, das_range, das_error_msg, OobExcept, OobComment, OutOfBand, das_url, das_credential, DasHttpResp, the iterators DasDsIter, DasDsUniqIter, DasDsCubeIter, DasAryIter and DasUriIter, and the six pairs in value.h (das_byte_seq, das_cbyte_seq, das_real_array, das_creal_array, das_int_array, das_cint_array) |
| Tables the application fills by value | 6 | StreamHandler, DasCodex, DasSelector, DasOutput, DasUriSegDef, DasUriField |
| Vendored parser records | 13 | the ten das_json structs, the three dastoml structs |

Every other heap object (DasIO, DasStream, DasDs, DasDim, DasVar, DasAry,
DasForm, DasProp, DasCodec, DasNode, DasPkt, DasPlane, DasDft, DasPsd,
DasEnc) is opaque.

### Who reaches into the public structs

- **Utilities**, 256 accesses.  StreamHandler callback slots by name (the
  struct's purpose), the iterators' `pDs`, `pAry` and `shape`, DasOutput
  `sUnits`, das_datum `units` and `vt`, das_range `dBeg` and `dEnd`, DasUriIter
  `nRanges` and `bDone`, das_error_msg,
  OobExcept, OobComment, and dastoml records in the config readers.  The 53
  `nType` and most `sPath` hits are the CDF utilities' own structs, not
  OobExcept or das_url.
- **das2py**, 37 accesses.  das_datum `units`, das_error_msg for Python
  exception text, and json records for catalog node contents.
- **das2dlm**, 151 accesses.  json and toml records (`type`, `value`),
  das_datum `units`, iterator `shape`, and the http records: das_url `sHost`,
  `sPort`, `sPath`, `sQuery`, `sDataset`, DasHttpResp `url`, das_credential
  `sServer`, `sRealm`, `sDataset`.
- **das2js**, 112 accesses.  dastoml and json records (`len`, `s`, `value`),
  iterator `pDs`, OobComment, OobExcept and das_error_msg.

### Downstream code that does not compile against master

Reaches into structs that are opaque, found by grepping for their field
names, since the script cannot attribute them:

- das2py: DasDs `lArrays`, `uArrays`, `lDims`, `uDims`, `sGroupId`; DasDim
  `aRoles`, `aVars`, `uVars`; DasAry `nRank`, `uFlags`; DasNode `pDom`, `sURL`.
- das2dlm: DasDs `lDims`, `uDims`, `lArrays`, `uArrays`; DasDim `aRoles`,
  `aVars`, `uVars`.
- das2js: DasVarBin `pLeft`, `pRight`.
- das2py: Das2Dft `uLen`, now DasDft_length().

Each has an accessor: DasDs_numAry, DasDs_getAry, DasDim_numVars,
DasDim_getVarByIdx, DasDim_getRoleByIdx, DasVarBin_left, DasVarBin_right,
DasVarBin_op.

Calls to names with no public declaration (the script's last section, less
the downstream projects' own DasRender, DasWasmRdr and DasJdo_toPyObj):

| Name | Called by | Replacement |
|---|---|---|
| DasVar_gen | das2py, das2js | DasVar_isBin, DasVar_getAry, DasVar_idxMap |
| DasGen_type, DasGen_elemType | das2py, das2js | DasVar_getAry() != NULL, DasVar_elemType |
| DasGenSeq_intercept, DasGenSeq_interval | das2js | none; a sequence's parameters have no public form |
| DasForm_kindStr | das2py, das2js | DasForm_kind |
| DasFormGeneric_paramAt | das2py, das2js | DasForm_paramAt, which returns a DasProp |
| DasFormGeneric_kind | das2py | DasForm_kind |
| DasDsBldr_getStream | das2py, das2js | DasIO_takeStream |
| del_DasStream | das2py, das2js | del_DasDesc |
| DasStream_getNPktDesc | das2py | DasStream_numDesc |
| DasNode_subNode | das2py | DasNode_subCount, DasNode_subName, DasNode_subStub |
| DasIO_model | das2py | das2_compat.h entry exists |
| DasVarAry_getArray | das2dlm | das2_compat.h entry exists |
| DasAry_disownElemens | das2dlm | DasAry_disownElements, the v2.3 misspelling |
| new_Dft, del_Dft, Dft_*, new_Psd, del_Das2Psd, Psd_* | das2py | new_DasDft, del_DasDft, DasDft_*, new_DasPsd, del_DasPsd, DasPsd_* |
| new_CredMngr, CredMngr_* | das2py, das2dlm | new_DasCredMngr, DasCredMngr_* |

### Struct-reaching accessor macros: 7

A macro that expands to `(P)->field` freezes that field, because the expansion
lands in application code.

| Macro | Header | Callers outside the library |
|---|---|---|
| DasCodex_storage, _path, _mime, _properties, del_DasCodex | codex.h | none |
| das_datum_valid | datum.h | 1 utility |
| DAS_FIELD_SZ | defs.h | 5 utilities; a sizeof helper that reaches no library struct |

## 4. Recommendations

Ordered by leverage against risk.  Everything in 4.1 becomes a break after
the tag.

### 4.1 Before the tag

1. **Version the fill-by-value tables.**  StreamHandler is the one struct
   every client fills in by hand, and DasCodex is the planned extension point
   for application supplied codecs.  Neither can grow.  A leading
   `size_t uSize` or `int nVersion` that the consumer checks lets a slot be
   added in 3.x without breaking a client compiled against 3.0.

2. **DasCodex macros to functions.**  The five `DasCodex_*` macros have no
   outside caller, so converting them now costs nothing and leaves the
   struct free to change.

3. **Make the remaining heap objects opaque.**  DasUriTplt and DasCredMngr
   have no downstream field reaches.

4. **Hide the http records**: das_url, DasHttpResp and das_credential.
   das2dlm reaches into all three, but it binds the v2.3 versions and has to
   be reworked for `das3/` anyway, so its migration is the moment to give it
   accessors.  `new_DasIO_http` and `new_DasIO_url` constructors, opening a
   stream straight from a URL, would remove most of the reason das2dlm and
   das2py touch these at all.

5. **Downstream breadcrumbs.**  Every name in the replacement table above
   gets either a das2_compat.h entry or a note in the downstream project.  A
   poison macro naming the replacement is the right form where the
   replacement is not a drop-in.

### 4.2 Un-export the plumbing

The 211 library-internal functions are of two kinds, and only one should be
trimmed.

**Plumbing**: encode and decode steps, buffer init helpers and property record
internals that exist because the library is split across translation units.
These belong in `*_p.h` headers.  About 60 functions:

- desc.h: `DasDesc_encode2`, `_encode3`, `_encode3Bare`, `_getTypeByIdx3`,
  `new_DasDesc`.
- stream.h: `DasDesc_decode`, `DasStream_encode2`, `_encode3`,
  `new_DasStream_str`, `DasStream_clonePkt`, `_rmDesc`, `_cacheShapes`.
- datset.h: `DasDs_decodeData`, `_cacheShape`, `_recBytes`.
- var.h: `DasVar_encode`.
- oob.h: all 8 encode, decode and init functions.
- packet.h and plane.h: `DasPkt_decodeData`, `_validate`, `_copyPlanes`,
  `new_DasPkt_xml`, `DasPlane_decodeData`, `_encode`, `_encodeData`,
  `new_DasPlane_empty`, `new_DasPlane_pairs`.
- enc.h: `DasEnc_equals`, `_hash`.
- prop.h: `DasProp_escapeSize`, `_xmlValue`, `dasprop_unescapeSep`.
- buffer.h: `DasBuf_initReadOnly`, `_initReadWrite`, `_wrapWrite`,
  `_writeFrom`, `_writeSpace`, `_readOffset`, `_strip`.
- io.h: `DasIO_queryExcept`, `_serverExcept`, `_sendLog`, `_closeNoData`,
  `new_DasIO_str`.  Utilities should be using send.h for these.
- util.h: `das_errdisp_get_lock`, `_release_lock`, `das_error_func`,
  `das_error_func_fixed`, `das_store_str`, `das_tokncpy`, `das_memset`.
- iter.h: the four `_init` and `_next` pairs behind the public iterator
  macros.

**Genuine API with no caller yet**, which stays exported.  Examples:
`DasDesc_getBool`, `DasDesc_getDouble`, `DasDesc_setBool`, the `dt_*` time
helpers, `das_lib_version`, `das_b64_decode`, `DasIO_getModel`,
`DasIO_getOutVer`, `DasIO_setLogLvl`, `DasIO_setTaskSize`, `DasAry_setFill`,
`DasAry_setId`, `DasDim_degenerate`, `DasVar_idxMap`, `DasVarBin_op`,
`DasVarBin_left`, `DasVarBin_right`, `new_DasVarCompSeq`, the form_rot.h
accessors.  The test is whether a reasonable application would ever call it;
the plumbing above fails that test.

### 4.3 Delete the three unused exports

`DasAry_cmp`, `DasBuf_paragraph`, `DasIO_getLogLvl`.  Each is defined, so
each is a real symbol with no caller.

### 4.4 The das2 legacy layer

plane.h and packet.h are 64 exported functions with no downstream caller,
used by the das2 utilities.  Both structs are opaque.  The das2 format is
frozen, so beyond the plumbing in 4.2 the layer stays as it is, documented as
legacy.

### 4.5 Tests-only functions

127 functions, mostly in the newest modules: req.h (18), source.h (18), dim.h
(8), array.h (8) and var.h (8).  These are API the design called for, with
the tests as first consumer.  No action, but the form_cplx.h and form_vec.h
entries are worth a glance: a formalism function that only a test calls may
be an extension hook nobody needs.

## 5. A trimmed 3.0

Applying 4.1 through 4.3:

| | now | after |
|---|---|---|
| Installed headers | 50 | 50 |
| `DAS_API` functions | 854 | about 790 |
| Public struct definitions | 43 | about 38 |
| Struct-reaching macros | 7 | 1 (DAS_FIELD_SZ) |

The remaining exposure is the stack records, the versioned tables and the
vendored parser records, each public for the reason below.

## 6. Why the remaining structs stay public

- **Stack records.**  A das_datum is produced for every element a reader pulls
  through `DasVar_get()`, an iterator is declared per loop and das_time is
  passed by value everywhere.  C offers two ways to hide those layouts and
  both cost something.  Heap allocating them puts a malloc in the inner loop.
  The pthread_mutex_t trick, a sized aligned byte blob with the real struct in
  the .c and a compile-time size check, hides the layout but freezes the size,
  loses debugger field names and turns every field read into a function call,
  since C99 cannot inline across translation units without exposing the body.
  These are records on purpose, like `struct tm`, and the header should say
  so and freeze them deliberately, as das_time does.
- **Tables.**  Filled by value, so the layout is the contract.  Versioned, see
  4.1 item 1.
- **Vendored.**  json.h and toml.h expose the records of the parsers they
  wrap.  Wrapping them again is writing an API over an API for no client who
  asked.

### Naming follows the split, with a few strays

Stack records are snake_case (das_time, das_datum, das_range, das_error_msg,
das_byte_seq) and heap objects are CamelCase (DasDs, DasAry, DasStream).
Where the typedef name differs from the struct tag, as in `struct das_var` for
DasVar, the tag is snake_case and the typedef CamelCase, which is consistent
with the rule since the typedef is what applications spell.

The strays are stack records and tables with CamelCase names:

- StreamHandler, a callback table filled by value; `das_stream_handler` or
  similar would fit the rule.
- OobExcept, OobComment and OutOfBand, declared on the stack by every utility
  that raises an exception.
- The iterators DasDsIter, DasDsUniqIter, DasDsCubeIter, DasAryIter and
  DasUriIter, stack objects by design, for the streaming discipline.
- DasBuf and DasHttpResp, stack records.
- DasSelector, DasOutput, DasUriSegDef and DasUriField, tables.

Renaming any of these is a source break, so it is a before-the-tag item or
never.  das2_compat.h carries aliases of exactly this kind (StreamDesc for
DasStream), so the mechanism for a soft landing exists.
