# Source lists shared by Linux.mak and Windows.mak
#
# This file is read by both GNU make and nmake, so stick to the subset of
# syntax they share: plain '=' assignments, '#' comments, and backslash line
# continuation.  No ':=', '+=', conditionals or functions.
#
# Every entry carries its directory and extension.  GNU make strips them with
# notdir/basename, nmake swaps them out with $(VAR:old=new) substitution, which
# can replace a prefix or suffix but cannot add one.  Use forward slashes.

# Library ####################################################################

LIB_SRCS = das3/das1.c das3/array.c das3/buffer.c das3/builder.c das3/cli.c \
 das3/codec.c das3/codex.c das3/credentials.c das3/dataset.c \
 das3/dataset_hdr2.c das3/dataset_hdr3.c das3/datum.c das3/descriptor.c \
 das3/dft.c das3/dimension.c das3/dsdf.c das3/encoding.c das3/http.c \
 das3/io.c das3/iterator.c das3/json.c das3/toml.c das3/log.c das3/node.c \
 das3/oob.c das3/operator.c das3/packet.c das3/plane.c das3/processor.c \
 das3/property.c das3/send.c das3/stream.c das3/time.c das3/tt2000.c \
 das3/units.c das3/utf8.c das3/util.c das3/value.c \
 das3/uri.c das3/generator.c das3/variable.c das3/var_bin.c \
 das3/form_vec.c das3/form_pos.c \
 das3/form.c das3/form_lin.c das3/form_pt.c das3/form_cplx.c \
 das3/form_rot.c

LIB_HDRS = das3/defs.h das3/time.h das3/das1.h das3/util.h das3/log.h \
 das3/buffer.h das3/utf8.h das3/value.h das3/units.h \
 das3/operator.h das3/datum.h das3/array.h das3/encoding.h das3/descriptor.h \
 das3/dimension.h das3/dataset.h das3/plane.h das3/packet.h das3/stream.h \
 das3/processor.h das3/property.h das3/oob.h das3/io.h das3/iterator.h \
 das3/builder.h das3/dsdf.h das3/credentials.h das3/http.h das3/dft.h \
 das3/json.h das3/toml.h das3/node.h das3/cli.h das3/send.h das3/uri.h \
 das3/codec.h das3/codex.h das3/core.h das3/variable.h \
 das3/form.h das3/form_rot.h das3/form_lin.h das3/form_pt.h \
 das3/form_cplx.h das3/form_vec.h das3/form_pos.h das3/das2_compat.h \
 das3/spice.h

# spice.h has no library code behind it, its functions compile into the
# program that includes it and links CSPICE.  So the library is the same with
# or without SPICE=yes, which only adds the programs below.

# Programs ###################################################################

# One source file per program, the program is named after it.  Programs that
# need extra source files (das2_bin_ratesec, das2_psd) get explicit rules.

UTIL_SRCS = utilities/das1_inctime.c utilities/das2_prtime.c \
 utilities/das1_fxtime.c utilities/das2_ascii.c utilities/das2_bin_avg.c \
 utilities/das2_bin_avgsec.c utilities/das2_bin_peakavgsec.c \
 utilities/das2_from_das1.c utilities/das2_from_tagged_das1.c \
 utilities/das1_ascii.c utilities/das1_bin_avg.c utilities/das2_bin_ratesec.c \
 utilities/das2_psd.c utilities/das2_hapi.c utilities/das2_histo.c \
 utilities/das2_cache_rdr.c utilities/das3_node.c utilities/das3_csv.c \
 utilities/das3_info.c utilities/das3_text.c utilities/das3_from_das2.c \
 utilities/das_fetch.c

SPICE_UTIL_SRCS = utilities/das3_spice.c

CDF_UTIL_SRCS = utilities/das3_cdf.c utilities/das3_from_cdf.c \
 utilities/das2_from_cdf.c

TEST_SRCS = test/TestUnits.c test/TestArray.c test/TestDs.c test/TestBuilder.c \
 test/TestAuth.c test/TestCatalog.c test/TestTT2000.c test/ex_das_cli.c \
 test/ex_das_ephem.c test/TestCredMngr.c test/TestV3Read.c test/TestFeed.c \
 test/TestProp.c \
 test/TestIter.c test/TestUri.c test/TestFilter.c test/TestValue.c \
 test/TestRaggedEncode.c test/TestGen.c test/TestForm.c test/TestCplx.c \
 test/TestVar.c test/TestVarSubset.c test/TestDim.c test/TestDatum.c \
 test/TestDesc.c test/TestCompat.c test/TestOob.c

SPICE_TEST_SRCS = test/TestSpice.c
