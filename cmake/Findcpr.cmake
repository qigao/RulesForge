# FindCPR.cmake
# This module finds the CPR library.

# Set the CPR_ROOT variable to specify the root installation directory
# Ensure CPR_ROOT is defined before using this module.

if(NOT DEFINED CPR_ROOT)
    message(
        FATAL_ERROR
            "CPR_ROOT is not defined. Please set it to your CPR installation path."
    )
endif()

# Define paths for include and library directories
set(CPR_INCLUDE_DIR ${CPR_ROOT}/include)
set(CPR_LIBRARY ${CPR_ROOT}/lib)

# Find the include directory for cpr.h
find_path(CPR_INCLUDE_DIR NAMES cpr/cpr.h PATHS ${CPR_INCLUDE_DIR})

# Find the CPR library
find_library(CPR_LIBRARY NAMES cpr PATHS ${CPR_LIBRARY})

# Handle standard package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    cpr REQUIRED_VARS CPR_LIBRARY CPR_INCLUDE_DIR FOUND_VAR CPR_FOUND
)

# Create an imported target if found and not already defined
if(CPR_FOUND AND NOT TARGET cpr::cpr)
    add_library(cpr::cpr UNKNOWN IMPORTED)
    set_target_properties(
        cpr::cpr
        PROPERTIES
            IMPORTED_LOCATION
            "${CPR_LIBRARY}/cpr.lib" # Adjust if necessary for your platform (e.g., .a or .dylib)
            INTERFACE_INCLUDE_DIRECTORIES
            "${CPR_INCLUDE_DIR};${CPR_INCLUDE_DIR}/.."
    )
endif()
