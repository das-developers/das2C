# Finds the libraries libdas3 links against and lists them in DAS_DEP_LIBS.
#
# Included by the top CMakeLists.txt and by the installed das2CConfig.cmake,
# so a downstream project finds the same libraries the build did.  The
# includer defines das_find_dep(), which is find_package(... REQUIRED) in the
# build and find_dependency() in das2CConfig.cmake.  zlib is linked publicly
# and isn't in the list.

das_find_dep(ZLIB)
das_find_dep(OpenSSL)

# vcpkg ships config files for expat and fftw3; many Linux distributions ship
# only pkg-config files for fftw3
find_package(expat CONFIG QUIET)
if(TARGET expat::expat)
	set(_das_expat expat::expat)
else()
	das_find_dep(EXPAT)
	set(_das_expat EXPAT::EXPAT)
endif()

find_package(FFTW3 CONFIG QUIET)
if(TARGET FFTW3::fftw3)
	set(_das_fftw FFTW3::fftw3)
else()
	das_find_dep(PkgConfig)
	pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
	set(_das_fftw PkgConfig::FFTW3)
endif()

if(WIN32)
	das_find_dep(PThreads4W)
	set(_das_threads PThreads4W::PThreads4W)
else()
	das_find_dep(Threads)
	set(_das_threads Threads::Threads)
endif()

set(DAS_DEP_LIBS ${_das_expat} ${_das_fftw} OpenSSL::SSL OpenSSL::Crypto ${_das_threads})
if(WIN32)
	list(APPEND DAS_DEP_LIBS ws2_32 Crypt32 Advapi32 User32)
else()
	list(APPEND DAS_DEP_LIBS m)
endif()

# NAIF doesn't allow CSPICE to be redistributed, so it isn't installed with
# das2C.  A downstream project can point CSPICE_INC and CSPICE_LIB at its own
# copy if the one das2C was built against has moved.
if(DAS_WITH_SPICE AND NOT TARGET das2C::cspice)
	add_library(das2C::cspice STATIC IMPORTED)
	set_target_properties(das2C::cspice PROPERTIES
		IMPORTED_LOCATION "${CSPICE_LIB}"
		INTERFACE_INCLUDE_DIRECTORIES "${CSPICE_INC}"
	)
endif()
