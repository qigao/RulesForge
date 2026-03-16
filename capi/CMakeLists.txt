include(GenerateExportHeader)

# Version information (keep in sync with rule_forge.h)
set(RULEFORGE_VERSION_MAJOR 0)
set(RULEFORGE_VERSION_MINOR 3)
set(RULEFORGE_VERSION_PATCH 0)

# Define the target name for the shared library
set(CAPI_TARGET_NAME rule_forge)

# Add the shared library target
add_library(
    ${CAPI_TARGET_NAME} SHARED src/rule_forge.cpp rfl_value_bridge.cpp
                               binary_splitters.c
)

target_compile_definitions(${CAPI_TARGET_NAME} PRIVATE SHARED_CXX)

# Include directories for the CAPI. PUBLIC: Only the C API header - this is what
# consumers need
target_include_directories(
    ${CAPI_TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)
# PRIVATE: Internal implementation headers - not exposed to consumers
target_include_directories(
    ${CAPI_TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/rulesforge/include
)

target_link_libraries(
    ${CAPI_TARGET_NAME}
    PRIVATE RulesForge rfl_parser
    PUBLIC TurboNet::Utils
)
set_target_properties(${CAPI_TARGET_NAME} PROPERTIES FOLDER capi/)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Link against pthread for WASM if threading is used Emscripten provides its
    # own pthread library
    target_link_libraries(${CAPI_TARGET_NAME} PRIVATE pthread)
elseif(ANDROID)
    # Android specific settings Set the C++ Standard Library to be used. Common
    # options: c++_static, c++_shared, gnustl_static, gnustl_shared,
    # stlport_static, stlport_shared c++_static (libc++_static.a) is generally
    # recommended for static linking.
    set_target_properties(
        ${CAPI_TARGET_NAME}
        PROPERTIES ANDROID_STL c++_static
                   ANDROID_NATIVE_API_LEVEL 21 # Minimum API level for Android
                                               # NDK
    )
    # Link against Android specific libraries if needed, e.g., log for logging
    # target_link_libraries(${CAPI_TARGET_NAME} PUBLIC log android)
endif()
add_subdirectory(tests)
add_subdirectory(examples)
# Set output directory for the shared library
set_target_properties(
    ${CAPI_TARGET_NAME}
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
               ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
# Install rules (optional, but good practice for a library)
install(
    TARGETS ${CAPI_TARGET_NAME}
    EXPORT RulesForgeTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(
    DIRECTORY include/
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.h"
)

# --- CMake Package Config ---
include(CMakePackageConfigHelpers)

# Generate version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfigVersion.cmake"
    VERSION
        ${RULEFORGE_VERSION_MAJOR}.${RULEFORGE_VERSION_MINOR}.${RULEFORGE_VERSION_PATCH}
    COMPATIBILITY SameMajorVersion
)

# Export targets
install(
    EXPORT RulesForgeTargets
    FILE RulesForgeTargets.cmake
    NAMESPACE RulesForge::
    DESTINATION lib/cmake/RulesForge
)

# Configure and install the config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RulesForgeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfig.cmake"
    INSTALL_DESTINATION lib/cmake/RulesForge
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfigVersion.cmake"
        DESTINATION lib/cmake/RulesForge
)
