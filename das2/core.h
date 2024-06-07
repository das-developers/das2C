/* Copyright (C) 2012-2021 Chris Piker <chris-piker@uiowa.edu>
 *               2004-2007 Jeremy Faden <jeremy-faden@uiowa.edu> 
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

/** @file core.h A roll-up header for the core Das2 C-Library */

/**
 * \mainpage
 *
 * <p><i>Das</i> streams are a self-describing, streamable format that allows
 * for the transmission of large and complex data sets between different systems
 * and programming environments. Unlike common file formats, streaming data
 * formats do not provide total lengths embedded in array headers, and they
 * do not provide arrays as a single continuous block of memory, instead everything
 * is record oriented, including the number of records!  Stream processors are
 * able to perform calculations on data as it passes from input to output. 
 * Handling data as streams allows for processing effectively infinite datasets
 * on modest hardware.  Stream data processing is the heart of Das.
 * </p>
 * 
 * <p>
 * Das systems have been around for a while.  This <a href="https://das2.org/das2overview2020piker.mp4">
 * a short video</a> provides a historical overview of das processing. 
 * </p>
 * <p> 
 * The initial version, now called <i>das1</i> was designed by Larry Granroth at
 * U. Iowa in 1996 and provided web forms for driving server-side processing
 * that creating static plots which were then displayed in a browser.  Processing
 * was broken in to <b>readers</b> which generated data streams and <b>plotters</b>
 * which provided graphical output. Readers were space-mission specific, but plotters
 * were not. This basic division continues through das3.
 * </p>
 * <p>Work on the now widely deployed <i>das2</i> system began in 2002 with the goal
 * of bringing mission agnostic interactive display and analysis tools to the
 * desktop. A precise description of das v2.2 streams can be found in the
 * <a href="https://github.com/das-developers/das2docs/tree/master/das2.2.2-ICD">
 * das v2.2.2 Iterface Control Document</a> on github.  The most successful
 * outgrowth of das2 is the feature-rich <a href="https://autoplot.org">Autoplot</a>
 * Java application.
 * </p>
 * 
 * <h2>This Version</h2>
 * 
 * <p>This edition of das2C kicks off support for <i>das3</i> streams
 * via the new serial.c and codec.c modules.  It also provides support for the
 * fault-tolerant das federated catalog via node.c, and many more new features.
 * Supporting more complex datasets and reduction algorithms required a new data
 * which is depicted in the diagram below.  Keeping the following container 
 * hierarchy in mind will help while buired deep in code.
 * </p>
 * 
 * \image html das_containers.png
 * 
 *
 * <p>External projects providing other new das3 tools include:
 * <ul>
 *   <li><i>dasFlex</i> for streaming data in multiple formats via self-advertised APIs</li>
 *   <li><i>dasTelem</i> for auto-parsing raw CCSDS instrument packets from PostgreSQL, and eventially</li>
 *   <li><i>dasView</i> which will bring rich interactions back to the browser.</li>
 * </ul>
 * </p>
 * 
 * <p>
 * In addition to the C library, a small collection of stream processing programs
 * are included in the utilities folder. These are:
 * <ul>
 *   <li>das1_ascii - Convert das1 binary streams to text</li>
 *   <li>das1_bin_avg - Reduce the size of das1 streams by averaging data in the X direction</li>
 *   <li>das2_ascii - Convert das2 binary streams to text</li>
 *   <li>das1_bin_avg - Reduce the size of das2 streams by averaging data in the X direction</li>
 *   <li>das2_bin_avgsec - Reduce the size of das2 streams by averaging data in time bins</li>
 *   <li>das2_bin_peakavgsec - Another reducer, this one produces both averages and max values in time</li>
 *   <li>das2_bin_ratesec - Provide a das2 stream data rate per event second summary</li>
 *   <li>das2_cache_rdr - Read from "database" of pre-reduced das2 streams</li>
 *   <li>das2_from_das1 - Upconvert das 1.0 streams to das 2.2 format</li>
 *   <li>das2_from_tagged_das1 - Upconvert das v1.1 streams to das 2.2 format.</li>
 *   <li>das2_hapi - Convert a das2 stream to a Heliphysics API stream.</li>
 *   <li>das2_histo - Covert a das2 stream to a stream of histograms</li>
 *   <li>das2_psd - Convert a das2 amplitude stream to a das2 Power Spectral Density stream</li>
 *   <li>das3_cdf - Write das v2 & v3 streams as CDF files.</li>
 *   <li>das3_node - Get the location and REST API of a das3 data source</li>
 * </ul>
 * </p>
 *
 * <h2>Reading Streams</h2>
 * <p>A das stream is typically read by:
 * <ol>
 * <li>Making an HTTP request via das_http_getBody() (optional)</li>
 * <li>Creating a ::DasIO object</li>
 * <li>Registering your stream handling callbacks via DasIO_addProcessor()</li>
 * <li>Calling DasIO_readAll() read the stream an invoke your callbacks</li>
 * </ol>
 * or, if you want to break with the stream processing mentality and just spool
 * all data in RAM:
 * <ol>
 * <li>Making an HTTP request via das_http_getBody() (optional) </li>
 * <li>Creating a ::DasIO object</li>
 * <li>Creating a ::DasDsBldr object and passing it into DasIO_addProcessor() </li>
 * <li>Calling DasIO_readAll() to process your input.</li>
 * <li>Calling DasDsBldr_getDataSets() to get a list of ::DasDs (das dataset) objects.</li>
 * </ol>
 * </p>
 * 
 * <p>
 * The following example is a minimal stream reader that prints information
 * about all datasets found in a das stream file.
 * @code
 * #include <das/core.h>
 * 
 * #define PROG_ERR 63  // be nice to the shell, don't use 0 here 
 * 
 * int main(int argc, char** argv)
 * {
 *   das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
 *   if(argc < 2)
 *     return das_error(ERR_RET, "Input file name missing");   
 *   
 *   DasIO* pIn = new_DasIO_file("das_example", argv[1], "r");
 *   DasDsBldr* pBldr = new_DasDsBldr();
 *   DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
 *   
 *   DasIO_readAll(pIn);
 * 
 *   size_t uDataSets = 0;
 *   DasDs** pDataSets = DasDsBldr_getDataSets(pBldr, &uDataSets);
 *   
 *   if(uDataSets == 0)
 *     daslog_info_v("No datasets found in %s", argv[1]);
 *   
 *   char sBuf[2048] = {'\0'};
 *   for(size_t u = 0; u < uDataSets; ++u){
 *     DasDs_toStr(pDataSets[u], sBuf, 2047); // one less insures null termination
 *     daslog_info(sBuf);
 *   }
 *   return 0;
 * }
 * @endcode
 * 
 * @note This example doesn't bother to free unneeded memory since it's 
 *   built as a standalone program and the OS will free it on exit. 
 *   When working in long running environments del_DasDs() and related
 *   functions should be called to free memory when it's no longer needed.
 *
 * <p>
 * Here's an example of building the program via gcc:
 * <pre>
 * gcc -o test_prog test_prog.c libdas3.0.a -lfftw -lssl -lcrypto -lexpat -lpthread -lz -lm
 * 
 * cl.exe /nologo /Fe:test_prog.exe test_prog.c das3.0.lib fftw3.lib zlib.lib libssl.lib \
 *                                  libcrypto.lib expatMD.lib Advapi32.lib User32.lib \
 *                                  Crypt32.lib ws2_32.lib pthreadVC3.lib
 * </pre>
 * In all likelyhood you'll need to use "-L" or "/LIBPATH" to provide the location
 * of the libraries above unless you've copied them to the current directory.
 * 
 * Here's an example for reading one of the das2 streams out of the included <b>test</b>
 * directory:
 * 
 * <pre>
 * ./test_prog test/das2_ascii_output1.d2t
 * </pre>
 * </p>
 *
 * <h2>Relationship to other <i>das</i> Libraries</h2>
 * 
 * Das2C is used by the following external projects:
 * <ul>
 *   <li><a href="https://github.com/das-developers/das2py">das2py</a> for fast
 *   reading of binary data directly into NumPy arrays.
 *   </li>
 *   <li><a href="https://github.com/das-developers/das2dlm">Das2DLM</a> to
 *   provide a native extension module for the Interactive Data Language (IDL)
 *   </li>
 *   <li><a href="https://github.com/das-developers/das2D">das2D<a> for D language
 *   support.
 *   </li>
 *   <li><a href="https://sddas.org">SDDAS</a> for reading data provided by das servers
 *   </li>
 * </ul> 
 *
 * Hopefully das2C is useful for your projects as well.
 */

#ifndef _das_core_h_
#define _das_core_h_

/* das 2/3 Libraries, use das2/das1.h to just use old packet and time handling */
#include <das2/defs.h>
#include <das2/util.h>
#include <das2/encoding.h>
#include <das2/buffer.h>
#include <das2/value.h>
#include <das2/units.h>
#include <das2/time.h>
#include <das2/tt2000.h>
#include <das2/operator.h>
#include <das2/datum.h>
#include <das2/descriptor.h>
#include <das2/plane.h>
#include <das2/packet.h>
#include <das2/oob.h>
#include <das2/stream.h>
#include <das2/processor.h>
#include <das2/io.h>
#include <das2/dsdf.h>
#include <das2/array.h>
#include <das2/variable.h>
#include <das2/dimension.h>
#include <das2/dataset.h>
#include <das2/iterator.h>
#include <das2/builder.h>
#include <das2/dft.h>
#include <das2/log.h>
#include <das2/credentials.h>
#include <das2/http.h>
#include <das2/node.h>
#include <das2/frame.h>
#include <das2/vector.h>
#include <das2/serial.h> /* might not need to be exposed */
#include <das2/codec.h>  /* might not need to be exposed */

/* Add a utility for handling UTF-8 as an internal string format, though
   almost all string manipulation algorithms get by without this even when
	the strings contain utf-8 characters */
#include <das2/utf8.h>

#endif /* _das_core_h_ */
