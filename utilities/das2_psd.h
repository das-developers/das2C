/* Copyright (C) 2017-2020  Chris Piker <chris-piker@uiowa.edu>
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

#ifndef _das2_psd_H_
#define _das2_psd_H_

/* Get the global DFT length (how many point in the xform */
size_t dftLen();

/* Get the global DFT slide (how many points to scoot over after an xform */
size_t dftSlide();

/* Get the global PSD calculator */
Das2Psd* psdCalc();


/* Each type of packet processor inherits it's handler information from
   this structure.  The only thing defined in the base class is an onData
	function */
typedef struct packet_handler{
	DasErrCode (*onData)(
		struct pkt_proc_info* pInfo, PktDesc* pPdIn, DasIO* pOut,
		StreamDesc* pSdOut
	);
} PktHandler;


/* transformation details.
 *    One of these is needed per input <xscan> style packet type.
 *    Since different xscan packet lengths (but not rates) can get flattened by
 *    the code, a single transform info may be share by multiple output <yscan>
 *    packets.
 * 
 *    Many of these may be needed per input <y> style packet type.  This is
 *    because x-multi-y is a flattened dataset that may hide multiple packet
 *    lengths and sampling rates.
 */
typedef struct dft_info {
	int nDftIn;        /* Number of input point for real signal DFT */
	int iMinDftOut;    /* Minimum PSD index to output, usually 0 */
	int iMaxDftOut;    /* Maximum PSD index to output, usually len/2 + 1 */
	double rYOutScale; /* Factor to get frequency output in hertz */
	double rZOutScale; /* Factor to normalize DFT results */
} DftInfo;

void init_fft_info(fft_info* pThis, int nDftLen)
{
	pThis->iMinDftOut = 0;
	pThis->iMaxDftOut = nDftLen/2 + 1;
	pThis->rYOutScale = 1.0;
	pThis->rZOutScale = 1.0;
}

PktDesc* hasMatchingPktDef(DasIO* pOut, StreamDesc* pSd, PktDesc* pPd);


#endif /* _das2_psd_H_ */
