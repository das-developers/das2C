/* Copyright (C) 2026  Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

/* *************************************************************************
  
   das3_from_das2: Up-convert a das2 stream to das3 with optional composite
     variable creation

**************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das3/core.h>

#define PROG "das3_from_das2"
#define PERR (DASERR_MAX + 1)

/* ************************************************************************* */
/* Globals */

/* None yet */

/* ************************************************************************* */
void prnHelp()
{
   printf(
"SYNOPSIS\n"
"   " PROG " - upconvert das2 streams, optionally adding variable formalisms\n"
"\n"
"USAGE\n"
"   " PROG " [-m META_FILE] < INFILE > OUTFILE\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a filter, it reads a das2 stream on standard input and writes\n"
"   a das3 stream on standard output. Typically only the data headers are\n"
"   modified while values pass trough unchanged. Compared to das2 streams, das3\n"
"   streams provide a much richer set of metadata to identify the relavent\n"
"   mathematical formalisms of collections of values.  Das3 has the concept\n"
"   if external, bulk-dataset array indexes and internal indicies for composite\n"
"   values.  For example in das2 a stream of time dependent vector values such as:\n"
"   \n"
"     time[0] lat[0] long[0], altitude[0]\n"
"     time[1] lat[1] long[1], altitude[1]\n"
"     time[2] lat[2] long[2], altitude[2]\n"
"   \n"
"   are read as one dependent variable (Time) and three independent variables (lat,\n"
"   long, altitude) with no relation between them.  In a das3 stream, these can be\n"
"   explicitly denoted as a geographic vector in standard coordinates (φ, λ, h) above\n"
"   a denoted reference body with altitudes defined by a spsecific referance surface.\n"
"\n"
"   Obviously the extra das3 metadata must come from somewhere. When run with `-m`\n"
"   a META_FILE can be used to alter the headers output for each dataset. Metafiles\n"
"   may add simple items such as extra stream properties, or you can use them to \n"
"   group scalars in defined formalisms such as vectors, rotations, quaternions,\n"
"   etc."
"\n"
"   A META_FILEs are written in TOML (Tom's Own Minimal Language), and an example\n" 
"   is provided below as an introduction.  See das3_metafile(5) for details\n"
"\n"
"      ---------------------------------------------------------------------\n"
"      # Provide vector defs when upconverting Juno fluxgate das2 streams\n"
"\n"
"      # Global stream properties\n"
"      [[props]]\n"
"      {name = \"note\", value=\"Upconverted from das2 by das3_from_das v1.0\"}\n"
"\n"
"      # A dataset section, may be ommited if stream only has one\n"
"      [[dataset]]\n"
"      name = 'fgm'     # Sets the output name\n"
"      sourcePacket = 1 # determines which input packets apply\n"
"\n"
"      # rename the lone data physical dimension\n"
"      [[dataset.dim]]\n"
"      name = \"fgm_payload\"\n"
"\n"
"      # Properties added to the physical dimension\n"
"      [[dataset.dim.props]]\n"
"      {name=\"notes\" value=\"Data are actually OB (outboard) or IB (inboard)\"}\n"
"\n"
"      # Grouping das2 planes into Vector composite values\n"
"      [[dataset.dim.var]]\n"
"      name = \"fgm\"\n"
"      sourcePlanes = ['Bx','By','Bz']  # determine which input planes apply\n"
"      kind = 'vector'                  # determine which operations apply\n"
"      ops.frame = 'JUNO_FGM_IB'        # operation properties to add\n"
"      ops.body  = 'JUNO'\n"
"      ---------------------------------------------------------------------\n"
"\n"
"   Note that metafiles *only* change the headers, packet values flow through\n"
"   unaltered.  This make " PROG " rather fast since there are no calculations\n"
"   in hot loops.\n"
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL, --log=LEVEL\n"
"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"               'warning', 'error' in order of decreasing verbosity. All log\n"
"               messages go to the standard error channel. Defaults to 'info'.\n"
"\n"
"   -m META_FILE, --meta-file META_FILE\n"
"               Provide header metadata over and above the default upconvert\n"
"               output.  See das3_metafile(5) for details.\n"
"\n"
"EXAMPLES\n"
"   1. Upconvert Juno FGM data to a das3 vector stream and write it as text:\n"
"\n"
"      fgm_l3_rdr 2016-01-01 2016-01-02 | " PROG " -m my_meta.toml | das3_text\n"
"\n"
"   2. Write TRACERS Search Coil spectral densities to a simple CDF file:\n"
"\n"
"      msc_reader 2026-05-04T12:00 2026-05-05T18:00 | das2_psd 1024 1 \\\n"
"         | " PROG " | das3_cdf -o msc_psd.cdf\n"
"\n"
"MAINTAINER\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das3_spice, das3_text, das3_cdf, das2_from_cdf, das3_from_cdf\n"
"\n"
);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{

   /* Exit on errors, log info messages and above */
   das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

   prnHelp();

   return 0;
}