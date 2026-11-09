/* Copyright (C) 2024-2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file prop_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.  Properties are packed into
 * storage owned by their container (DasDesc, das_form_param_buf); the calls
 * below lay out and size that storage.  Applications set properties through
 * the DasDesc_set*() calls and read them with the accessors in prop.h.
 */

#ifndef _das_prop_p_h_
#define _das_prop_p_h_

#include <das3/prop.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Variable length: the name and value are packed into buffer, which is
   over-allocated to dasprop_memsz().  The bit layout of flags is charted in
   prop.c. */
struct das_prop {
	uint64_t  flags;       /* property type, validity, value offset */
	das_units units;       /* Units, if any */
	char      buffer[16];  /* The property name and value, typically
	                          over-malloc'ed */
};

/* The value offset (name + null) must fit NLEN and the total (name + null +
   value + null) must fit TLEN (bit layout in prop.c):
   126 + 1 + 130943 + 1 = 131071 = 2^17 - 1 */
#define DASPROP_NMAX_SZ     126
#define DASPROP_VMAX_SZ     130943

/* If these bits are 0, the property is invalid, ignore it. */
#define DASPROP_VALID_MASK  0x00000003

/** Get required storage space for a property given a name and value.
 * Note the space is requirements are not sum of the string lengths.
 */
size_t dasprop_memsz(const char* sName, const char* sValue);

/** Flexible das1, das2 and das3 compatible property memory initializer
 *
 * @param pBuf A byte buffer that is at least dasprop_memsz() bytes long
 *
 * @param sType The data type of the property, can be one of:
 *    - @b boolean       (das2 & 3)
 *    - @b double, real  (das2, das3)
 *    - @b realRange     (das3)
 *    - @b doubleArray   (das2)
 *    - @b Datum         (das2, anything can have units in das3)
 *    - @b DatumRange    (das2)
 *    - @b int,integer   (das2, das3)
 *    - @b integerRange  (das3)
 *    - @b String,string (das2, das3)
 *    - @b Time,datetime (das2, das3)
 *    - @b TimeRange,datetimeRange (das2,das3)
 *
 * @param uType An alternate and more efficient method of specifying
 *    the property type.  If sType is NULL, this is read instead.
 *    To set uType or together one constant from each set below.
 *
 *    Item type: DASPROP_STRING, DASPROP_BOOL, DASPROP_INT, DASPROP_REAL,
 *       DASPROP_DATETIME
 *
 *    Multiplicity: DASPROP_SINGLE, DASPROP_RANGE, DASPROP_SET
 *
 * @param sName The name of the property, can be no longer then 126
 *              bytes. This is a looser restriction then associated XSDs.
 *
 * @param sValue The data value, can be no longer then 130,943 bytes.
 *
 * @param cSep  For array values, this (in addition to possible whitespace)
 *              is the separator between values.  Ignored if Multiplicity
 *              is not DASPROP_SET.
 *
 * @param units The units for this value.  If the type is Datum or DatumRange
 *              this value will be ignored, otherwise if this value is NULL
 *              then UNIT_DIMENSIONLESS will be assigned.
 *
 * @param nStandard.  One of 1, 2 or 3 for das1, das2, or das3.  For
 *              das2 & 3, property names may only consist of the characters
 *              [a-z][A-Z][0-9] and '_'.
 *
 * @memberof DasProp
 */
DasErrCode init_DasProp(
   ubyte* pBuf, size_t uBufSz, const char* sType, ubyte uType, const char* sName,
   const char* sValue, char cSep, das_units units, int nStandard
);

/* A property that has passed every check in init_DasProp() and has been
   measured, but not yet written.  Containers that can't undo a slot
   allocation (DasDesc) plan first, so a refused property never costs them a
   slot or the old value.  sName and sValue are borrowed: nothing may free
   or move them between dasprop_plan() and dasprop_write(). */
typedef struct das_propplan {
	uint64_t    uFlags;
	das_units   units;
	const char* sName;
	size_t      uNameSz;
	const char* sValue;    /* past any trimmed leading whitespace */
	size_t      uValSz;    /* bytes of sValue to store */
	char        cSep;
	bool        bCollapse; /* sValue is collapsed while writing */
} das_propplan;

/* Run the checks of init_DasProp() and fill in pPlan, writes nothing else.
   Arguments are as for init_DasProp(), less the buffer. */
DasErrCode dasprop_plan(
	das_propplan* pPlan, const char* sType, ubyte uType, const char* sName,
	const char* sValue, char cSep, das_units units, int nStandard
);

/* Write a planned property.  Can't fail, pBuf must hold at least
   dasprop_memsz() bytes for the planned name and original value. */
void dasprop_write(const das_propplan* pPlan, ubyte* pBuf);

/** Return the memory footprint of a property, never less than sizeof(DasProp)
 * @memberof DasProp
 */
size_t DasProp_size(const DasProp* pProp);

/** Mark this property as invalid, this erases the type information and
 * is thus a non-reversible operation
 * @memberof DasProp
 */
void DasProp_invalidate(DasProp* pProp);


/* Moved from prop.h: library plumbing, not API */

/** Get the size of needed escape buffer if property contains illegal XML chars
 * 
 * @return 0 if an XML escape buffer is not needed, otherwise the buffer size
 *         DasProp_xmlValue() needs, terminating NUL included
 * @memberof DasProp
 */
size_t DasProp_escapeSize(const DasProp* pProp);

/** Get the string value for a property with illegal XML characters escaped
 * 
 * @param pProp pointer to the property in question
 * @param sBuf  pointer to a buffer to hold the translated property
 * @param uLen  length of the buffer which holds the translated property,
 *        DasProp_escapeSize() is enough.  The result is NUL terminated.
 * 
 * @returns A pointer to the escaped property value, which may be the
 *          original internal storage if XML escapes weren't needed or
 *          it may be the given buffer.  
 * 
 *          If XML escape codes are needed and the given buffer is too small
 *          das_error is called, which may (depending on the error disposition)
 *          halt the program.
 * 
 * @memberof DasProp
 */
const char* DasProp_xmlValue(const DasProp* pProp, char* sBuf, size_t uLen);

/** Get a das2 type string for this property  
 * @memberof DasProp
 */
const char* DasProp_typeStr2(const DasProp* pProp);

/* Decode an |Hx| property-value separator (possibly a backslash escape: \n \t
 * \r \\) to a single byte.  Empty/NULL -> '\0' so callers fall back to guessing.
 * 0x00 is not representable here -- property values are string-typed, so the
 * larger |Pd| packet-separator set (which can carry 0x00) needs its own decoder.
 * Free function (no DasProp receiver), hence the lowercase name. */
char dasprop_unescapeSep(const char* sSep);

#ifdef __cplusplus
}
#endif

#endif /* _das_prop_p_h_ */
