# CTest definitions, included by the top CMakeLists.txt.  These follow
# test_main, test_spice and test_cdf in Linux.mak and Windows.mak.  Tests run
# from the top of the source tree and write their output to the build
# directory.  Labels: main, spice, cdf, e.g. ctest -L cdf
#
# The golden-file comparisons at the end of test_main in Linux.mak are shell
# scripts and don't run here yet.

set(_bd "${CMAKE_BINARY_DIR}")
set(_redirect "${CMAKE_CURRENT_LIST_DIR}/redirect.cmake")

# _das_test(<name> <label> <program> [args...])
function(_das_test name label prog)
	add_test(NAME ${name} COMMAND ${prog} ${ARGN}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	)
	set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()

# _das_redir(<name> <label> [IN file] [OUT file] <program> [args...]) for
# commands that use < or >, which add_test can't do on its own
function(_das_redir name label)
	cmake_parse_arguments(PARSE_ARGV 2 _r "" "IN;OUT" "")
	set(_defs)
	if(_r_IN)
		list(APPEND _defs "-DIN=${_r_IN}")
	endif()
	if(_r_OUT)
		list(APPEND _defs "-DOUT=${_r_OUT}")
	endif()
	_das_test(${name} ${label} ${CMAKE_COMMAND} ${_defs} -P "${_redirect}" -- ${_r_UNPARSED_ARGUMENTS})
endfunction()

# Stream fixtures for TestV3Read and TestFeed, the same sets as Linux.mak.
# Left out on purpose: examples/ex28_*, test/streams/notimp_* and reject_*
file(GLOB _v2 CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}"
	test/streams/*.d2s test/streams/*.d2t examples/*.d2s examples/*.d2t
)
file(GLOB _v3 CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}"
	examples/*.d3b examples/*.d3t test/streams/*.d3b test/streams/*.d3t
)
list(FILTER _v3 EXCLUDE REGEX "^examples/ex28_|^test/streams/(notimp|reject)_")

# test_main ##################################################################

# Variable layer first: fastest, highest value, no goldens
foreach(_t TestValue TestDatum TestGen TestForm TestCplx TestVar TestVarSubset
           TestDim TestUnits TestTT2000 TestArray)
	_das_test(${_t} main ${_t})
endforeach()

# Datasets, streams and iteration
_das_test(TestDs main TestDs)
_das_test(TestBuilder main TestBuilder)
_das_test(das3_info_load main das3_info -q examples/ex07_cassini_rpws_wbr.d2s)
_das_test(TestV3Read main TestV3Read test/streams/tag_test.dNt ${_v2} ${_v3})
_das_test(TestFeed main TestFeed test/streams/tag_test.dNt ${_v2} ${_v3})
_das_test(TestIter main TestIter)
_das_test(TestRaggedEncode main TestRaggedEncode
	examples/ex30_cassini_ragged_notlast.d3b examples/ex31_efi_ragged_vec.d3b
	examples/ex32_marsis_2d_ragged.d3b examples/ex34_ragged_fixstr.d3b
	examples/ex38_wbr_wfrm_tags.d3b examples/ex39_sandwich.d3b
)
_das_test(TestFilter main TestFilter)
_das_test(TestUri main TestUri "${_bd}/uri_tplt")
_das_test(TestCredMngr main TestCredMngr "${_bd}")
_das_test(TestCat main TestCat)
_das_test(TestHttp main TestHttp)
_das_test(TestOob main TestOob)
foreach(_v 2 3)
	if(_v EQUAL 2)
		set(_ext d2s)
	else()
		set(_ext d3t)
	endif()
	_das_redir(TestOob_send${_v} main OUT "${_bd}/oob_send.${_ext}"
		$<TARGET_FILE:TestOob> send ${_v}
	)
	_das_test(TestOob_read${_v} main TestOob read "${_bd}/oob_send.${_ext}")
	set_tests_properties(TestOob_read${_v} PROPERTIES DEPENDS TestOob_send${_v})
endforeach()
_das_test(TestCompat main TestCompat "${_bd}")
_das_redir(das3_csv_varlen main
	IN examples/ex21_tracers_cdpu_status.d3b OUT "${_bd}/ex21_tracers_cdpu_status.csv"
	$<TARGET_FILE:das3_csv>
)

# das_fetch: a saved catalog node and its example 0 become a query URL, no
# network.  The full offline suite is test/das_fetch_test.sh (bash only).
_das_test(das_fetch_url main das_fetch -C none url test/das_fetch/geomag_flex_0.7.json 0)
set_tests_properties(das_fetch_url PROPERTIES PASS_REGULAR_EXPRESSION
	"read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
)

# test_spice #################################################################

if(DAS_WITH_SPICE)
	_das_test(TestSpice spice TestSpice)
endif()

# test_cdf ###################################################################
#
# Can't test CDF creation by comparison due to the embedded time stamps.  The
# das3_cdf runs write CDFs that TestCdf then reads back.  They're CTest
# fixture setups, so TestCdf runs after them and "ctest -R TestCdf" runs them
# too.
#
# Known exclusions from the das3_cdf fixture set:
#   ex22: a rank 3 grid with coordinates that vary along the grid indices only;
#         no ISTP DEPEND layout fits it, a bespoke converter is the answer.
#   ex27: carries a byte run (blob), which das3_cdf does not write yet.

if(DAS_WITH_CDF)
	set(_cdf_steps)

	# _das_cdf(<name> <args>...) - one das3_cdf run
	macro(_das_cdf name)
		_das_test(${name} cdf das3_cdf -l warning ${ARGN})
		list(APPEND _cdf_steps ${name})
	endmacro()

	_das_cdf(cdf_ex12 -i examples/ex12_sounder_xyz.d3t -o "${_bd}" -r)

	# Composite variables in CDF output (labels, units, C-order flattening)
	_das_cdf(cdf_ex15 -i examples/ex15_vector_frame.d3t -o "${_bd}" -r)
	_das_cdf(cdf_ex15_mapped -i examples/ex15_vector_frame.d3t
		-o "${_bd}/ex15_mapped.cdf" -r -p FRAME_NAME:COORD_FRAME
	)

	# das extension attributes and a CDF round trip
	_das_cdf(cdf_ex20 -i examples/ex20_tracers_aci.d3t -o "${_bd}" -r)
	_das_redir(cdf_ex20_from_cdf cdf OUT "${_bd}/ex20_rt.d3b"
		$<TARGET_FILE:das3_from_cdf> -l warning "${_bd}/ex20_tracers_aci.cdf"
	)
	list(APPEND _cdf_steps cdf_ex20_from_cdf)
	_das_cdf(cdf_ex20_rt -i "${_bd}/ex20_rt.d3b" -o "${_bd}/ex20_rt.cdf" -r)
	set_tests_properties(cdf_ex20_from_cdf PROPERTIES DEPENDS cdf_ex20)
	set_tests_properties(cdf_ex20_rt PROPERTIES DEPENDS cdf_ex20_from_cdf)

	_das_cdf(cdf_ex40 -i examples/ex40_rotation.d3b -o "${_bd}" -r)
	_das_cdf(cdf_ex41 -i examples/ex41_quaternion.d3b -o "${_bd}" -r)
	_das_cdf(cdf_ex42 -i examples/ex42_plain_tensor.d3b -o "${_bd}" -r)
	_das_cdf(cdf_ex43 -i examples/ex43_msc_complex_cal.d3b -o "${_bd}" -r)
	_das_cdf(cdf_ex43_keep -i examples/ex43_msc_complex_cal.d3b
		-o "${_bd}/ex43_keep.cdf" -r -k
	)

	set_tests_properties(${_cdf_steps} PROPERTIES FIXTURES_SETUP cdf_files)
	_das_test(TestCdf cdf TestCdf "${_bd}")
	set_tests_properties(TestCdf PROPERTIES FIXTURES_REQUIRED cdf_files)
endif()
