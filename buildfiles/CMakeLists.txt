cmake_minimum_required(VERSION 3.16.3) # Default on ubuntu20.
project(das2C VERSION 3.0.0 LANGUAGES C )

set(CMAKE_C_STANDARD 99)

# Setup the vcpkg includes (example courtesy of K. Steele)
if(NOT DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED VCPKG_ROOT AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Variable VCPKG_ROOT must be defined via command line or environment variable, or CMAKE_TOOLCHAIN_FILE must point to vcpkg.cmake.")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})    
        set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    else()
        set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    endif()
endif()
message("Toolchain: ${CMAKE_TOOLCHAIN_FILE}")

set(VCPKG_TARGET_TRIPLET x64-windows-static)

# Define the library
file(GLOB_RECURSE lLibFiles ${PROJECT_SOURCE_DIR}/das2/*.c ${PROJECT_SOURCE_DIR}/das2/*.h)
add_library("das2" STATIC ${lLibFiles})
target_include_directories("das2" PUBLIC ${PROJECT_SOURCE_DIR})

# Now it's dependencies
message("Prefix: ${CMAKE_PREFIX_PATH}")

find_package(expat CONFIG REQUIRED)
target_link_libraries(main PRIVATE expat::expat)

find_package(FFTW3 CONFIG REQUIRED)
target_link_libraries(main PRIVATE FFTW3::fftw3)


#add_executable(TestArray test/TestArray.c)

