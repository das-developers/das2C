# Remove what the last cmake --install from this build directory put in place:
#
#   cmake -DBUILD_DIR=<build dir> -P uninstall.cmake
#
# Run through the uninstall target.  cmake --install records each file it
# installs in install_manifest.txt (install_manifest_<component>.txt with
# --component), only the most recent install for each.  The das3 and das2C
# directories are removed too once they're empty, other directories under the
# install prefix are left alone.

file(GLOB _manifests "${BUILD_DIR}/install_manifest*.txt")
if(NOT _manifests)
	message(STATUS "Nothing to uninstall, no install_manifest*.txt in ${BUILD_DIR}")
	return()
endif()

set(_dirs)
foreach(_manifest IN LISTS _manifests)
	file(STRINGS "${_manifest}" _files)
	foreach(_file IN LISTS _files)
		if(EXISTS "${_file}" OR IS_SYMLINK "${_file}")
			message(STATUS "Removing ${_file}")
			file(REMOVE "${_file}")
		endif()
		get_filename_component(_dir "${_file}" DIRECTORY)
		get_filename_component(_name "${_dir}" NAME)
		if(_name MATCHES "^(das3|das2C)$")
			list(APPEND _dirs "${_dir}")
		endif()
	endforeach()
	file(REMOVE "${_manifest}")
endforeach()

list(REMOVE_DUPLICATES _dirs)
foreach(_dir IN LISTS _dirs)
	file(GLOB _left "${_dir}/*")
	if(IS_DIRECTORY "${_dir}" AND NOT _left)
		message(STATUS "Removing ${_dir}")
		file(REMOVE_RECURSE "${_dir}")
	endif()
endforeach()
