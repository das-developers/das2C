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

/** @file codec_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.  The wire plumbing below is
 * driven by DasDs_decodeData() and DasDs_encodeData(); applications choose
 * encodings with codec.h and the DasDs_add*Codec() calls instead.
 */

#ifndef _das_codec_p_h_
#define _das_codec_p_h_

#include <das3/codec.h>
#include <das3/enc.h>   /* DASENC_FMT_LEN */

#ifdef __cplusplus
extern "C" {
#endif

#define DASENC_VALID    0x0001

struct das_codec {

	bool bResLossWarn; /* If true, the resolution loss warning has already been emitted */

	uint32_t uProc; /* Internal processing flags setup on the call to _init */

	int nAryValSz;  /* The size of each array value in internal buffer */

	const char* sEncType;  /* a canonical DAS_ENC_* global, or NULL */

	int16_t nBufValSz;  /* Width of a single value in the external buffer, -1 is  */

	das_val_type vtBuf; /* The value type in the external buffer */

	const char* sSemantic; /* Intended meaning: a canonical DAS_SEM_* global, or NULL */

	DasAry* pAry;  /* The array for which values are encoded/decoded */

	ubyte nSep;
	char sSepSet[VARIDX_MAX];  /* Split strings on these chars by rank */

	ubyte nExtRagged;  /* Number of the variable's EXTERNAL ragged indices inside a packet
	                      (excludes the packet-framed streaming index and any internal axis
	                      such as a string's chars).  This is the count of idxTerm run
	                      terminators the var needs; set from the index= shape at creation. */

   bool bItemLen;       /* Ignore nSep, read val lengths from packet data */

	uint32_t uMaxString; /* If we are storing fixed strings, this is set */

	das_units timeUnits; /* If ascii times are to be stored as an integral type
									this is needed */

	/* For output, the sprintf string (if UTF8) or the stream encode type */
	char sOutFmt[DASENC_FMT_LEN];

	char* pOverflow;   /* If the size of a variable length value breaks the */
	size_t uOverflow;  /* small vector assumption, extra space is here */

};

/** Has the memory for this encoder been initialized? 
 * 
 * @memberof DasCodec 
 */
#define DasCodec_isValid(pCd) (((pCd)->uProc & DASENC_VALID)==(DASENC_VALID))

/* Re-initialize a live codec in place, keeping whatever is NULL or 0.
 *
 * Releases the old state (array reference, overflow buffer) that
 * init_DasCodec() would orphan.  Run terminators, the ragged index count
 * and trim survive.  pAry is NOT checked for membership in any dataset, so
 * outside callers go through DasDs_updateCodec(), which resolves an array
 * id against the codec's own dataset. */
DasErrCode DasCodec_update(
	bool bRead, DasCodec* pThis, DasAry* pAry, const char* sEncType,
	int16_t nSzEach, ubyte cSep, das_units epoch, const char* sOutFmt
);

/** Initialize a serial buffer decoder/encoder
 * 
 * @param bRead if set to DASENC_READ, perform checks for value reading codecs
 *              if set to DASENC_WRITE, perform checks for value writing codecs
 *
 * @param pThis Storage for a codec that holds nothing: zeroed or never
 *        initialized.  init does not release earlier state, re-initialize a
 *        live codec with DasCodec_update().
 * 
 * @param pAry A pointer to the array which either receive or supply values.
 *        Values will be encoded so that they match the value type of the
 *        array. 
 *        @warning If the basic parameters of this array, such as it's value
 *        type or rank are changed, re-aim the codec with DasCodec_update().
 * 
 * @param sSemantic The purpose of the data to store in the buffer, should
 *        be one of 'bool','int','real','datatime','string'.  This determines
 *        the kinds of calculations that may be performed on the data once
 *        in memory. 
 * 
 * @param sEncType The basic encoding type of data in the buffer, one of:
 *        - byte   : 8-bit signed integer
 *        - ubyte  : 8-bit un-signed integer
 *        - BEint  : A signed integer 2+ bytes long, most significant byte first
 *        - BEuint : An un-signed integer 2+ bytes long MSB first
 *        - LEint  : Little-endian version of BEint
 *        - LEuint : Little-endian version of BEuint
 *        - BEreal : An IEEE-754 floating point value, MSB first
 *        - LEreal : An IEEE-754 floating point value, LSB first
 *        - utf8   : A string of text bytes
 * 
 * @param nSzEach the number of bytes in an item.  For variable length
 *        items (which is common with the utf8 encoding) use DASENC_ITEM_SEP,
 *        or DASENC_ITEM_LEN.
 * 
 * @param cSep A single byte used to mark the end of a byte sequence for
 *        string data.  By default any space character marks the end of
 *        a string.  Use 0 to ignore.
 * 
 * @param epoch If time data needs to be decoded from UTC strings an epoch
 *        will be needed.  Otherwise this field can be NULL
 * 
 * @param sOutFmt a printf style format string, may be NULL for input only
 *        codecs, or to have the initializer set a default output format
 *        string
 * 
 *   Typical strings for general
 *        data values would be: '%9.2e', '%+13.6e'.  In general strings 
 *        such as '%13.3f' should @b not be used as these aren't guaranteed
 *        to have a fix output width and your value strings may be truncated.
 * 
 * @returns DAS_OKAY if an decoder/encoder for can be created for the given
 *        arguments, an error code otherwise.
 * 
 * @note For 'string' semantic data where the last index in the array is 
 *       ragged DasAry_markEnd() will be called after each string is read.
 *       Otherwise, no string larger then the last index will be written
 *       and zeros will be appended to fill out the last index when reading
 *       data.
 * 
 * @memberof DasCodec 
 */
DasErrCode init_DasCodec(
	bool bRead, DasCodec* pThis, DasAry* pAry, const char* sSemantic, 
	const char* sEncType, int16_t uSzEach, ubyte cSep, das_units epoch, 
	const char* sOutFmt
);

/** Set codec to eat extra whitespace, ignored for not text decoding
 * @memberof DasCodec
 */
void DasCodec_eatSpace(DasCodec* pThis, bool bEat);

/** Enable/disable ltrim+rtrim of decoded values (var-width utf8, default on).
 * Distinct from eatSpace: trim keeps internal spaces, only stripping surrounding
 * pad.  Wired to <packet trim="...">.  A space-only string collapses to 
 * just "" which is a null string and is completely legal.
 * 
 * Read-side only.
 * @memberof DasCodec
 */
void DasCodec_setTrim(DasCodec* pThis, bool bTrim);

/** True if this codec trims (DASENC_TRIM).  Lets the writer re-emit a non-default
 * trim="false" so surrounding whitespace survives a round-trip.
 * @memberof DasCodec
 */
bool DasCodec_isTrim(const DasCodec* pThis);

/** Fix array pointer after a DasCodec memory copy
 * 
 * @param pThis A pointer to the new memory area filled via memcpy()
 * 
 * @param pAry The new array to associate with this codec
 * 
 * @memberof DasCodec
 */
void DasCodec_postBlit(DasCodec* pThis, DasAry* pAry);

/** Read values from a simple buffer into an array
 * 
 * Unlike the old das2 version, this encoder doesn't have a built-in number
 * of values it will always expect to read.  If no pre-determined number
 * of values is given in nExpect, then it will read until the buffer is 
 * exhausted.
 * 
 * To control the number of bytes read control nBufLen
 * 
 * @param pThis An encoder.  The pointer isn't constant because the
 *        encoder may have to allocate some memory for long, variable length
 *        text values, so internal state may change.
 *
 * @param pBuf A pointer to the memory to read
 *
 * @param nBufLen The length of the buffer parse into the array.  Note that
 *        even for string data the function tries to read nLen bytes.  Null
 *        values do not terminate parsing but do indicate the end of an 
 *        individual utf-8 encoded item.
 *
 * @param nExpect The number of values to try and read.  Reading less then 
 *        this does not trigger an error return.  If the caller considers
 *        reading less values then expect to be an error, compare *pValsRead 
 *        with the number provided for nExpect.  If any number of values 
 *        can be read, set this to -1.
 * 
 * @param pValsRead A pointer to a location to hold the number of values read
 *        or NULL.  If NULL, the number of values read will not be returned
 * 
 * @returns the number of unread bytes or a negative ERR code if a data conversion
 *        error occurred.
 * @memberof DasCodec 
 */
int DasCodec_decode(
	DasCodec* pThis, const ubyte* pBuf, int nBufLen, int nExpect, int* pValsRead
);

/** Read terminator-bounded (idxTerm) ragged runs from a buffer.
 *
 * The counterpart of DasCodec_decode for variable-count utf8 runs. 
 * Since the run lengths denoted by values and index terminator characters
 * the are unknowable up front. The codec consumes values until the
 * declared run terminators (sSepSet[1..], set via DasCodec_setIdxTerms)
 * close every level, calling DasAry_markEnd itself as each level closes.
 * 
 * A higher-level terminator also closes open lower levels (the collapsed
 * form).  The packet edge acts as the stream-index terminator: it closes
 * all open runs, but only the packet's last variable may lean on it.
 *
 * @param pThis a text codec with at least one idxTerm level attached
 * @param pBuf the read point, positioned at the start of this codec's runs
 * @param nBufLen bytes remaining in the packet
 * @param bLastVar true if this is the last variable in the packet, making
 *        the packet edge a legal run close
 * @param pValsRead if non-NULL, receives the number of values read
 * @returns the number of unread bytes, or a negative das error code
 * @memberof DasCodec
 */
int DasCodec_decodeRuns(
	DasCodec* pThis, const ubyte* pBuf, int nBufLen, bool bLastVar, int* pValsRead
);

#define DASENC_PKT_LAST 0x02
#define DASENC_IN_HDR   0x04

/** Write values from an array into a buffer, does not change the array 
 * 
 * The goal of this function is to emitt all data from continuous range of
 * indexes starting at a given point.  Examples of setting the start location:
 *
 * nDim=0, pLoc=NULL  as DIM0 => Emitt the entire array
 *
 * nDim=1, pLoc={I}   as DIM1_AT(I) => Emit all data for one increment of the 
 *                                     highest index
 *
 * nDim=2, pLoc={I,J} as DIM2_AT(I,J) => Emit all data for one increment of the
 *                                     the next highest index.
 *
 * To write all data for an array set: nDim = 0, pLoc = NULL (aka use DIM0 )
 * 
 * @param pThis The codec structure
 * 
 * @param pBuf The output receiver
 * 
 * @param nDim Part of the start location specification, see description above
 * 
 * @param pLoc Part of the start location specification, see description above
 * 
 * @param nExpect The number of items expected to be written.  If -1 then
 *        the output is variable length.  In the case of text items this is
 *        the number of strings, not the number of total characters
 * 
 * @param uFlags that affect the output.  The following are defined, mostly
 *        for text output:
 * 
 *        DASENC_PKTLAST - This is the last item output for for a packet
 *            Add a new line character after it if text.
 * 
 *        DASENC_INHDR - Encoding is being performed for a header, so
 *            don't emit too many items in a single row.
 *        
 * @returns The number of values written or a negative ERR code if a data
 *          conversion error occurred.
 */
int DasCodec_encode(
	DasCodec* pThis, DasBuf* pBuf, int nDim, ptrdiff_t* pLoc, int nExpect,
	uint32_t uFlags
);

/** Write one record's terminator-bounded (idxTerm) ragged run to a buffer.
 *
 * The write mirror of DasCodec_decodeRuns: emits every run under record
 * iRec with the full declared terminator stack at every boundary, including
 * against the packet edge (collapsed forms are read, never written).
 *
 * @param pThis a text codec with at least one idxTerm level attached
 * @param pBuf the output receiver
 * @param iRec the record (highest array index) to emit
 * @returns the number of values written, or a negative das error code
 * @memberof DasCodec
 */
int DasCodec_encodeRuns(DasCodec* pThis, DasBuf* pBuf, ptrdiff_t iRec);

/** Release the reference count on the array given to this encoder/decoder 
 * 
 * @memberof DasCodec 
 */
void fini_DasCodec(DasCodec* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_codec_p_h_ */
