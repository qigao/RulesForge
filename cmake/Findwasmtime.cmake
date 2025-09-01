# Findwasmtime.cmake
#
# Finds the wasmtime library and headers.
#
# This module defines:
#   WASMTIME_FOUND          - True if wasmtime is found
#   WASMTIME_INCLUDE_DIRS   - Include directories for wasmtime
#   WASMTIME_LIBRARIES      - Libraries to link against
#   wasmtime::wasmtime      - Imported target for wasmtime
#
# Hints:
#   Set WASMTIME_ROOT to the wasmtime installation directory to help locate it.
#
# Example usage:
#   find_package(wasmtime REQUIRED)
#   add_executable(my_program main.c)
#   target_link_libraries(my_program PRIVATE wasmtime::wasmtime)

include(FindPackageHandleStandardArgs)

# Find wasmtime include directory
find_path(WASMTIME_INCLUDE_DIR
    NAMES wasmtime.h
    HINTS
        ${WASMTIME_ROOT}/include
        $ENV{WASMTIME_ROOT}/include
        /usr/local/include
        /usr/include
    PATH_SUFFIXES
        wasmtime
)

# Find wasmtime library
find_library(WASMTIME_LIBRARY
    NAMES wasmtime libwasmtime
    HINTS
        ${WASMTIME_ROOT}/lib
        $ENV{WASMTIME_ROOT}/lib
        /usr/local/lib
        /usr/lib
)

# Set wasmtime variables
if (WASMTIME_INCLUDE_DIR AND WASMTIME_LIBRARY)
    set(WASMTIME_INCLUDE_DIRS ${WASMTIME_INCLUDE_DIR})
    set(WASMTIME_LIBRARIES ${WASMTIME_LIBRARY})
endif()

# Handle the QUIETLY and REQUIRED arguments and set WASMTIME_FOUND
find_package_handle_standard_args(wasmtime
    REQUIRED_VARS WASMTIME_LIBRARY WASMTIME_INCLUDE_DIR
)

# Create imported target if found
if (WASMTIME_FOUND AND NOT TARGET wasmtime::wasmtime)
    add_library(wasmtime::wasmtime UNKNOWN IMPORTED)
    set_target_properties(wasmtime::wasmtime PROPERTIES
        IMPORTED_LOCATION "${WASMTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WASMTIME_INCLUDE_DIR}"
    )
    # Handle Windows static linking by defining macros to disable dllimport
    if (WIN32 AND NOT BUILD_SHARED_LIBS)
        set_target_properties(wasmtime::wasmtime PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "WASM_API_EXTERN=;WASI_API_EXTERN="
        )
    endif()
endif()

mark_as_advanced(WASMTIME_INCLUDE_DIR WASMTIME_LIBRARY)
