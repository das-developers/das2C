# das2C CMake build
#
# This sits alongside the makefiles in buildfiles/, it doesn't replace them.
# The source lists come from buildfiles/sources.mak, so add new files there.
#
# Only tested on Windows with MSVC and vcpkg so far.  Nothing here is meant
# to be Windows-only, but on Linux and MacOS the makefiles are the supported
# build.
#
# Quick start, from a VS x64 Native Tools prompt with VCPKG_ROOT set:
#
#   cmake --preset default            Configure, vcpkg fetches the dependencies
#   cmake --build --preset default    Static library and programs
#   ctest --preset default            Run the tests
#   cmake --install build.cmake/default --prefix C:\opt
#   cmake --build --preset default --target uninstall
#
# Use the "dll" preset instead to add das3.dll.  Without presets, pass
# -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake, or
# just set VCPKG_ROOT and it's picked up below.
#
# Options (all default to ON except DAS_BUILD_DLL):
#
#   DAS_WITH_SPICE   SPICE support in the library, das3_spice, TestSpice
#   DAS_WITH_CDF     das3_cdf, das3_from_cdf, das2_from_cdf, TestCdf
#   DAS_BUILD_DLL    Also build das3.dll (libdas3.so elsewhere)
#   BUILD_TESTING    Build the test programs and register them with CTest
#
# CSPICE and CDF are found from CSPICE_INC + CSPICE_LIB and CDF_INC + CDF_LIB
# when set.  Otherwise, on Windows, they're downloaded into the build
# directory at configure time.  Elsewhere they're searched for on the system.

cmake_minimum_required(VERSION 3.20)

# Setup the vcpkg includes (example courtesy of K. Steele).  vcpkg is optional,
# without it the dependencies come from the system.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(DEFINED VCPKG_ROOT)
		set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
	elseif(DEFINED ENV{VCPKG_ROOT})
		set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
	endif()
endif()

# The usual first-run mistakes, caught here since CMake's own messages for
# them are confusing.  The presets build the toolchain path from VCPKG_ROOT.
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
	message(FATAL_ERROR
		"Toolchain file ${CMAKE_TOOLCHAIN_FILE} doesn't exist.  Is VCPKG_ROOT set "
		"to your vcpkg clone?  In PowerShell use $env:VCPKG_ROOT=..., since 'set' "
		"there makes a shell variable, not an environment variable."
	)
endif()
if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED CMAKE_C_COMPILER
   AND NOT DEFINED ENV{CC})
	find_program(_das_cl cl.exe)
	set(_das_have_cl "${_das_cl}")
	unset(_das_cl CACHE)
	if(NOT _das_have_cl)
		message(FATAL_ERROR
			"cl.exe isn't on the PATH.  Run vcvars64.bat in this window first, from "
			"cmd.exe, or use a Developer Command Prompt or Developer PowerShell."
		)
	endif()
endif()

# Static vcpkg dependencies and a static C runtime by default on Windows, the
# same as Windows.mak.  Must be set before project().  Without vcpkg, as under
# conda, the C runtime stays CMake's default DLL one (/MD) to match the
# dependency DLLs.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET
   AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
	set(VCPKG_TARGET_TRIPLET x64-windows-static CACHE STRING "vcpkg triplet")
endif()

# One version number, kept in vcpkg.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" _das_manifest)
string(JSON _das_version GET "${_das_manifest}" version-string)

project(das2C VERSION ${_das_version} LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DAS_WITH_SPICE "Build SPICE support and programs" ON)
option(DAS_WITH_CDF "Build the CDF programs" ON)
option(DAS_BUILD_DLL "Also build the library as a DLL / shared object" OFF)
option(BUILD_TESTING "Build the test programs" ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

##############################################################################
# Source lists, read from buildfiles/sources.mak

set(_das_srcmak "${CMAKE_CURRENT_SOURCE_DIR}/buildfiles/sources.mak")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_das_srcmak}")

file(READ "${_das_srcmak}" _das_mk)
string(REGEX REPLACE "\\\\\r?\n" " " _das_mk "${_das_mk}")   # join continuations
string(REGEX MATCHALL "[^\r\n]+" _das_lines "${_das_mk}")
foreach(_line IN LISTS _das_lines)
	if(_line MATCHES "^([A-Z_]+)[ \t]*=(.*)$")
		separate_arguments(_vals UNIX_COMMAND "${CMAKE_MATCH_2}")
		set(${CMAKE_MATCH_1} ${_vals})
	endif()
endforeach()

foreach(_var LIB_SRCS LIB_HDRS SPICE_LIB_SRCS UTIL_SRCS CDF_UTIL_SRCS TEST_SRCS)
	if(NOT ${_var})
		message(FATAL_ERROR "${_var} not found in ${_das_srcmak}")
	endif()
endforeach()

if(DAS_WITH_SPICE)
	list(APPEND LIB_SRCS ${SPICE_LIB_SRCS})
	list(APPEND LIB_HDRS ${SPICE_LIB_HDRS})
	list(APPEND UTIL_SRCS ${SPICE_UTIL_SRCS})
	list(APPEND TEST_SRCS ${SPICE_TEST_SRCS})
endif()

##############################################################################
# Compiler settings

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The makefiles keep assert() active in optimized builds and so do we.  For
# MSVC also put debug info in the objects (/Z7) so libdas3.lib carries it,
# leave inlining at the /O2 level as Windows.mak does, and skip incremental
# linking, which leaves 20 MB+ .ilk files.  Linking with /debug turns off
# removal of unused code, so optimized builds ask for it back (/OPT).
foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
	string(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_C_FLAGS_${_cfg} "${CMAKE_C_FLAGS_${_cfg}}")
endforeach()
if(MSVC)
	foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_${_cfg} "${CMAKE_C_FLAGS_${_cfg}}")
		string(REPLACE "/Ob1" "" CMAKE_C_FLAGS_${_cfg} "${CMAKE_C_FLAGS_${_cfg}}")
		foreach(_kind EXE SHARED)
			string(REGEX REPLACE "/INCREMENTAL( |$)" "/INCREMENTAL:NO\\1"
				CMAKE_${_kind}_LINKER_FLAGS_${_cfg} "${CMAKE_${_kind}_LINKER_FLAGS_${_cfg}}"
			)
			if(NOT _cfg STREQUAL "DEBUG")
				string(APPEND CMAKE_${_kind}_LINKER_FLAGS_${_cfg} " /OPT:REF /OPT:ICF")
			endif()
		endforeach()
	endforeach()

	add_compile_options(/wd4068)   # unknown pragma, i.e. #pragma GCC
	set(_das_wisdom "C:/ProgramData/fftw3/wisdom.dat")
else()
	add_compile_definitions(_XOPEN_SOURCE=600)
	add_compile_options(
		$<$<C_COMPILER_ID:GNU>:-Wall$<SEMICOLON>-Wno-deprecated-declarations$<SEMICOLON>-Wno-stringop-truncation$<SEMICOLON>-Wno-format-truncation>
	)
	set(_das_wisdom "/etc/fftw/wisdom")
endif()

# Programs, the library and the DLL all land flat in the build directory,
# the way the makefiles lay them out
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

##############################################################################
# Dependencies

# CSPICE and CDF, from the cache variables, a download, or the system
set(CSPICE_INC "" CACHE PATH "Directory holding SpiceUsr.h")
set(CSPICE_LIB "" CACHE FILEPATH "The CSPICE static library")
set(CDF_INC "" CACHE PATH "Directory holding cdf.h")
set(CDF_LIB "" CACHE FILEPATH "The CDF library")

set(_das_cspice_url "https://naif.jpl.nasa.gov/pub/naif/toolkit//C/PC_Windows_VisualC_64bit/packages/cspice.zip")
set(_das_cdf_url "https://spdf.gsfc.nasa.gov/pub/software/cdf/dist/cdf39_1/windows/cdf3.9.1_64bit_WinZip_Installer.zip")

# Download a zip once and keep only the parts we need
function(_das_fetch_zip name url dest)
	if(EXISTS "${dest}/.complete")
		return()
	endif()
	set(_zip "${CMAKE_BINARY_DIR}/_deps/${name}.zip")
	message(STATUS "Downloading ${name} from ${url}")
	file(DOWNLOAD "${url}" "${_zip}" STATUS _st)
	list(GET _st 0 _rc)
	if(NOT _rc EQUAL 0)
		list(GET _st 1 _msg)
		message(FATAL_ERROR "Couldn't download ${name}: ${_msg}")
	endif()
	file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${dest}" PATTERNS ${ARGN})
	file(REMOVE "${_zip}")
	file(TOUCH "${dest}/.complete")
endfunction()

if(DAS_WITH_SPICE AND NOT (CSPICE_INC AND CSPICE_LIB))
	if(WIN32)
		set(_d "${CMAKE_BINARY_DIR}/_deps")
		_das_fetch_zip(cspice "${_das_cspice_url}" "${_d}" cspice/include cspice/lib/cspice.lib)
		set(CSPICE_INC "${_d}/cspice/include" CACHE PATH "" FORCE)
		set(CSPICE_LIB "${_d}/cspice/lib/cspice.lib" CACHE FILEPATH "" FORCE)
	else()
		find_path(CSPICE_INC SpiceUsr.h PATH_SUFFIXES cspice)
		find_library(CSPICE_LIB NAMES cspice.a cspice)
		if(NOT (CSPICE_INC AND CSPICE_LIB))
			message(FATAL_ERROR "CSPICE not found, set CSPICE_INC and CSPICE_LIB or turn off DAS_WITH_SPICE")
		endif()
	endif()
endif()

if(DAS_WITH_CDF AND NOT (CDF_INC AND CDF_LIB))
	if(WIN32)
		set(_d "${CMAKE_BINARY_DIR}/_deps/cdf")
		_das_fetch_zip(cdf "${_das_cdf_url}" "${_d}" include lib/libcdf.lib)
		set(CDF_INC "${_d}/include" CACHE PATH "" FORCE)
		set(CDF_LIB "${_d}/lib/libcdf.lib" CACHE FILEPATH "" FORCE)
	else()
		find_path(CDF_INC cdf.h)
		find_library(CDF_LIB NAMES libcdf.a cdf)
		if(NOT (CDF_INC AND CDF_LIB))
			message(FATAL_ERROR "CDF not found, set CDF_INC and CDF_LIB or turn off DAS_WITH_CDF")
		endif()
	endif()
endif()

if(DAS_WITH_CDF)
	add_library(das2C_cdf STATIC IMPORTED)
	set_target_properties(das2C_cdf PROPERTIES
		IMPORTED_LOCATION "${CDF_LIB}"
		INTERFACE_INCLUDE_DIRECTORIES "${CDF_INC}"
	)
endif()

# The libraries libdas3 itself links against.  Shared with the installed
# das2CConfig.cmake so downstream projects find the same ones.
macro(das_find_dep)
	find_package(${ARGV} REQUIRED)
endmacro()
include(buildfiles/cmake/das2CDeps.cmake)

##############################################################################
# The library

# _das_lib(<target> <STATIC|SHARED>) - libdas3 from the same sources both ways
function(_das_lib targ kind)
	add_library(${targ} ${kind} ${LIB_SRCS} ${LIB_HDRS})
	target_include_directories(${targ} PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	)
	target_compile_definitions(${targ} PRIVATE WISDOM_FILE=${_das_wisdom})

	# Only zlib shows up in the public headers (das3/io.h)
	target_link_libraries(${targ} PUBLIC ZLIB::ZLIB PRIVATE ${DAS_DEP_LIBS})
	if(DAS_WITH_SPICE)
		target_link_libraries(${targ} PRIVATE das2C::cspice)
	endif()
endfunction()

_das_lib(das3 STATIC)
if(MSVC)
	set_target_properties(das3 PROPERTIES OUTPUT_NAME libdas3)  # libdas3.lib
endif()

set(_das_lib_targets das3)

if(DAS_BUILD_DLL)
	_das_lib(das3_shared SHARED)
	set_target_properties(das3_shared PROPERTIES OUTPUT_NAME das3)  # das3.dll + das3.lib
	target_compile_definitions(das3_shared PRIVATE BUILDING_DLL PUBLIC DAS_USE_DLL)
	list(APPEND _das_lib_targets das3_shared)
endif()

##############################################################################
# Programs

# _das_prog(<source> [extra sources...]) - program named after its first source
function(_das_prog src)
	get_filename_component(_name "${src}" NAME_WE)
	add_executable(${_name} ${src} ${ARGN})
	target_link_libraries(${_name} PRIVATE das3)
	set(_das_last_prog ${_name} PARENT_SCOPE)
endfunction()

set(_das_progs)
foreach(_src IN LISTS UTIL_SRCS)
	if(_src STREQUAL "utilities/das2_bin_ratesec.c")
		_das_prog(${_src} utilities/via.c)
	elseif(_src STREQUAL "utilities/das2_psd.c")
		_das_prog(${_src} utilities/send.c)
	else()
		_das_prog(${_src})
	endif()
	list(APPEND _das_progs ${_das_last_prog})
endforeach()

# Robert's tagged das1 reader breaks C99 strict aliasing rules, don't let gcc
# optimize it (MSVC doesn't assume strict aliasing)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(utilities/das2_from_tagged_das1.c
		PROPERTIES COMPILE_OPTIONS "-O0;-fno-strict-aliasing"
	)
endif()

if(DAS_WITH_SPICE)
	target_link_libraries(das3_spice PRIVATE das2C::cspice)
endif()

if(DAS_WITH_CDF)
	_das_prog(utilities/das3_cdf.c)
	_das_prog(utilities/das3_from_cdf.c utilities/cdfmodel.c)
	_das_prog(utilities/das2_from_cdf.c utilities/cdfmodel.c)
	foreach(_p das3_cdf das3_from_cdf das2_from_cdf)
		target_link_libraries(${_p} PRIVATE das2C_cdf)
	endforeach()
	list(APPEND _das_progs das3_cdf das3_from_cdf das2_from_cdf)
endif()

##############################################################################
# Tests, these mirror test_main, test_spice and test_cdf in the makefiles

if(BUILD_TESTING)
	enable_testing()

	foreach(_src IN LISTS TEST_SRCS)
		_das_prog(${_src})
	endforeach()
	if(DAS_WITH_SPICE)
		target_link_libraries(TestSpice PRIVATE das2C::cspice)
	endif()
	if(DAS_WITH_CDF)
		_das_prog(test/TestCdf.c)
		target_link_libraries(TestCdf PRIVATE das2C_cdf)
	endif()

	include(buildfiles/cmake/das2CTests.cmake)
endif()

##############################################################################
# Install
#
#   cmake --install <build dir> --component Runtime       Programs, schemas
#   cmake --install <build dir>                           Adds lib, headers,
#                                                         CMake package files
#   cmake --install <build dir> --component Debug         Only the .pdb files

install(TARGETS ${_das_progs} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

file(GLOB _das_schemas CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.xsd")
install(FILES ${_das_schemas} DESTINATION ${CMAKE_INSTALL_DATADIR}/das2C COMPONENT Runtime)

install(TARGETS ${_das_lib_targets} EXPORT das2CTargets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime   # das3.dll
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime   # libdas3.so
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
)
install(FILES ${LIB_HDRS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/das3 COMPONENT Development)

# Debug info (.pdb) for the programs and DLL, beside them in bin so debuggers
# find it.  Not part of a normal install, ask for it with --component Debug.
if(MSVC)
	set(_das_pdb_targets ${_das_progs})
	if(DAS_BUILD_DLL)
		list(APPEND _das_pdb_targets das3_shared)
	endif()
	foreach(_t IN LISTS _das_pdb_targets)
		install(FILES $<TARGET_PDB_FILE:${_t}> DESTINATION ${CMAKE_INSTALL_BINDIR}
			COMPONENT Debug EXCLUDE_FROM_ALL OPTIONAL
		)
	endforeach()
endif()

# So that other CMake projects can find_package(das2C) and link das2C::das3
set(_das_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/das2C)

install(EXPORT das2CTargets NAMESPACE das2C:: DESTINATION ${_das_cmake_dir}
	COMPONENT Development
)
configure_package_config_file(buildfiles/cmake/das2CConfig.cmake.in
	"${CMAKE_BINARY_DIR}/das2CConfig.cmake" INSTALL_DESTINATION ${_das_cmake_dir}
)
write_basic_package_version_file("${CMAKE_BINARY_DIR}/das2CConfigVersion.cmake"
	COMPATIBILITY SameMajorVersion
)
install(FILES
	"${CMAKE_BINARY_DIR}/das2CConfig.cmake"
	"${CMAKE_BINARY_DIR}/das2CConfigVersion.cmake"
	buildfiles/cmake/das2CDeps.cmake
	DESTINATION ${_das_cmake_dir} COMPONENT Development
)

# CMake has no uninstall of its own.  This removes the files the last
# cmake --install from this build directory recorded in install_manifest.txt
if(NOT TARGET uninstall)
	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/buildfiles/cmake/uninstall.cmake
		COMMENT "Removing installed das2C files"
	)
endif()
