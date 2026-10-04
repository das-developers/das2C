/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5.1
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

/* ****************************************************************************
 das2_from_cdf: Pull data from a CDF file series and output a das2 stream

 Why this is its own program.  We considered giving libdas3 the ability to
 write das2 streams from its DasDs data model and decided against it.  That
 would add long term complexity to the library on behalf of a format the
 old stream object model (PktDesc, PlaneDesc) already writes, and there are
 no plans to remove that model.  A tenet of the Unix philosophy is that a
 program should do one thing well.  Turning a pile of CDFs into a das2
 stream is this program's one job.  das3_from_cdf does the same for das3,
 and the CDF file model the two share lives in cdfmodel.h.

**************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>

#include "cdfmodel.h"

#define PROG "das2_from_cdf"

int main(int argc, char** argv)
{
	fprintf(stderr, "%s: not implemented yet, see das3_from_cdf\n", PROG);
	return PERR;
}
