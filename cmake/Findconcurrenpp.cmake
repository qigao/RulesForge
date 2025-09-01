# FindConcurrentCpp.cmake
# This module finds the ConcurrentCpp library.
# Set the CONCURRENT_CPP_ROOT variable to specify the root installation directory
# Ensure CONCURRENT_CPP_ROOT is defined before using this module.
if(NOT DEFINED CONCURRENT_CPP_ROOT)
  message(
    FATAL_ERROR
      "CONCURRENT_CPP_ROOT is not defined. Please set it to your ConcurrentCpp installation path."
  )
endif()

# Define paths for include and library directories
set(CONCURRENT_CPP_INCLUDE_DIR ${CONCURRENT_CPP_ROOT}/include)
set(CONCURRENT_CPP_LIBRARY ${CONCURRENT_CPP_ROOT}/lib)

# Find the include directory for concurrentcpp.h or any other relevant header file
find_path(CONCURRENT_CPP_INCLUDE_DIR NAMES concurrentcpp/concurrentcpp.h
          PATHS ${CONCURRENT_CPP_INCLUDE_DIR}
)

# Find the ConcurrentCpp library
find_library(
  CONCURRENT_CPP_LIBRARY NAMES concurrentcpp PATHS ${CONCURRENT_CPP_LIBRARY}
)

# Handle standard package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  concurrentcpp REQUIRED_VARS CONCURRENT_CPP_LIBRARY CONCURRENT_CPP_INCLUDE_DIR
  FOUND_VAR CONCURRENT_CPP_FOUND
)

# Create an imported target if found and not already defined
if(CONCURRENT_CPP_FOUND AND NOT TARGET concurrentcpp::concurrentcpp)
  add_library(concurrentcpp::concurrentcpp UNKNOWN IMPORTED)
  set_target_properties(
    concurrentcpp::concurrentcpp
    PROPERTIES
      IMPORTED_LOCATION
      "${CONCURRENT_CPP_LIBRARY}/libconcurrentcpp.lib" # Adjust if necessary for your platform (e.g., .lib or .dylib)
      INTERFACE_INCLUDE_DIRECTORIES
      "${CONCURRENT_CPP_INCLUDE_DIR};${CONCURRENT_CPP_INCLUDE_DIR}/.."
  )
else()
  message(WARNING "ConcurrentCpp not found.")
endif()
