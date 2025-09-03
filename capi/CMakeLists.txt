include(GenerateExportHeader)

# Define the target name for the shared library
set(CAPI_TARGET_NAME rule_forge)

# Add the shared library target
add_library(${CAPI_TARGET_NAME} SHARED
    src/rule_forge.cpp
)

target_compile_definitions(${CAPI_TARGET_NAME} PRIVATE DRILLS_CAPI_EXPORTS)
generate_export_header(${CAPI_TARGET_NAME}
    BASE_NAME RULEFORGE_CAPI
    EXPORT_MACRO_NAME RULEFORGE_CAPI_EXPORT
    NO_EXPORT_MACRO_NAME RULEFORGE_CAPI_NO_EXPORT
    DEPRECATED_MACRO_NAME RULEFORGE_CAPI_DEPRECATED
)
find_package(pegtl )


# Include directories for the CAPI.
 target_include_directories(${CAPI_TARGET_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include> # For installation
    ${CMAKE_SOURCE_DIR}/ruleforge/include
    ${CMAKE_SOURCE_DIR}/shared/pubcxx/include
)

target_link_libraries(${CAPI_TARGET_NAME}
     RuleForge  pubcxx
    hashmap   taocpp::pegtl
)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Link against pthread for WASM if threading is used
    # Emscripten provides its own pthread library
    target_link_libraries(${CAPI_TARGET_NAME} PUBLIC pthread)
elseif(ANDROID)
    # Android specific settings
    # Set the C++ Standard Library to be used.
    # Common options: c++_static, c++_shared, gnustl_static, gnustl_shared, stlport_static, stlport_shared
    # c++_static (libc++_static.a) is generally recommended for static linking.
    set_target_properties(${CAPI_TARGET_NAME} PROPERTIES
        ANDROID_STL c++_static
        ANDROID_NATIVE_API_LEVEL 21 # Minimum API level for Android NDK
    )
    # Link against Android specific libraries if needed, e.g., log for logging
    # target_link_libraries(${CAPI_TARGET_NAME} PUBLIC log android)
endif()
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
