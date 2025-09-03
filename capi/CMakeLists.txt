# drills/capi/CMakeLists.txt
# This CMakeLists.txt builds the C API as a shared library.

# Add the CAPI subdirectory to the build.
# This assumes that the parent CMakeLists.txt (at c:\projects\cpp\drills\)
# already includes this subdirectory using add_subdirectory(capi).

# Define the target name for the shared library
set(CAPI_TARGET_NAME drills_capi)

# Add the shared library target
add_library(${CAPI_TARGET_NAME} SHARED
    src/drills_capi.cpp
)
find_package(pegtl )


# Include directories for the CAPI.
# This needs to include the public CAPI header and the Drills core library headers.
target_include_directories(${CAPI_TARGET_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For drills_capi.h
    $<INSTALL_INTERFACE:include> # For installation
    # Assuming Drills core headers are in drills/include
    # You might need to adjust this path based on your project structure
    ${CMAKE_SOURCE_DIR}/drills/include
    ${CMAKE_SOURCE_DIR}/shared/pubcxx/include
)

# Link against the Drills core library.
# This assumes your main Drills library is built as a static or shared library
# and is named 'drills_core' or similar. Adjust as needed.
target_link_libraries(${CAPI_TARGET_NAME}
    drills pubcxx
    hashmap   taocpp::pegtl
)
add_subdirectory(tests)
add_subdirectory(examples)
# Set output directory for the shared library
set_target_properties(${CAPI_TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Install rules (optional, but good practice for a library)
install(TARGETS ${CAPI_TARGET_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
