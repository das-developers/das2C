# Run a program with stdin and/or stdout redirected to files, for CTest:
#
#   cmake [-DIN=<file>] [-DOUT=<file>] -P redirect.cmake -- <program> [args...]
#
# Exits non-zero if the program does.

set(_cmd)
set(_after_dashes FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_i RANGE 1 ${_last})
	if(_after_dashes)
		list(APPEND _cmd "${CMAKE_ARGV${_i}}")
	elseif(CMAKE_ARGV${_i} STREQUAL "--")
		set(_after_dashes TRUE)
	endif()
endforeach()

if(NOT _cmd)
	message(FATAL_ERROR "usage: cmake [-DIN=file] [-DOUT=file] -P redirect.cmake -- <program> [args...]")
endif()

set(_opts)
if(IN)
	list(APPEND _opts INPUT_FILE "${IN}")
endif()
if(OUT)
	list(APPEND _opts OUTPUT_FILE "${OUT}")
endif()

execute_process(COMMAND ${_cmd} ${_opts} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "${_cmd} exited with: ${_rc}")
endif()
