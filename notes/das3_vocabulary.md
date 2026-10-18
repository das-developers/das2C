# das3 value vocabulary -- element, item, component, value

Status: 2026-10-18.  Written after a survey of term usage across the schema,
the codec layer, the array layer and the dataset model, which found three
words each covering two meanings.  The identifiers and comments were brought
in line with the glossary below in the same commit that added this file.

## The four granularities

The library handles values at four distinct grain sizes.  Each layer only
sees some of them, which is proper layering, not an inconsistency -- but
each grain needs its own name.

* **element** -- one simple-typed slot in a backing buffer: one float, one
  int64, one ubyte of a string's run.  Enumerated by `das_elem_type`,
  reported by `DasVar_elemType()` and `das_datum_elemType()`, sized by
  `DasAry_valSize()`.  Never composite, never variable length.  This is
  also the word foreign bindings expect: a JavaScript TypedArray or a
  MATLAB matrix has "elements" of exactly this kind.

* **item** -- one encodable: the unit a codec reads or writes per transfer,
  what the schema's `numItems` counts and `itemBytes` widths.  Equal to one
  element for numeric types; equal to one whole byte run (one string, one
  blob) for `AS_STRING`/`AS_SUBSEQ` arrays.  See `DasAry_itemsIn()`, which
  documents the distinction: a string counts as ONE item no matter how many
  bytes it holds, while a vector's components each count.  The codec layer
  tops out here and knows nothing of composites.

* **component** -- one semantic member of a composite value: the "x" of a
  vector, the real part of a complex pair.  Carries a symbol
  (`DasVar_compSym()`) and a label (`DasVar_compLabels()`).  Storage-wise a
  component occupies one element; the word belongs to the dataset model and
  to presentation, not to storage.

* **value** -- the whole externally indexable thing at a dataset index
  (i,j,...): a double, a calendar time, a whole string, or a whole
  composite.  Classified by `das_val_type` (hence `vtComposite` exists
  there and NOT in `das_elem_type`) and boxed by `das_datum`.

## Rules of thumb

* If it has a byte size and a simple type, it is an *element*.
* If a codec moves one of it at a time, it is an *item*.
* If it has a symbol or a label, it is a *component*.
* If it has units and lives at a dataset index, it is a *value*.
* "cell" is retired; where old comments said cell, read element.

## Known remaining wrinkles

* `das_val_type` and `das_elem_type` agree numerically on 0..11 and code in
  a few places casts between them.  The cast is legal but the two enums
  answer different questions; see the note on `DasVar_elemType()`.
* `DasVar_serialType()` (renamed from `DasVar_element()`, which collided
  with the storage meaning of element) answers with the XML element name a
  variable serializes as: "scalar", "composite" or "bytes".  That is a
  structural classification, not a grain size, and is deliberately outside
  the list above.
