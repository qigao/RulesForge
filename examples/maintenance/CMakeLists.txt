cmake_add_source(
    maintenance_native_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
    "native_math.cpp" "*.h"
)
add_library(maintenance_math MODULE ${maintenance_native_sources})
target_link_libraries(maintenance_math PRIVATE Toad::PluginABI)
cmake_configure_target(maintenance_math FOLDER examples/maintenance)

cmake_add_source(
    maintenance_demo_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
    "maintenance_demo.cpp" "session.hpp"
)
add_executable(maintenance_demo ${maintenance_demo_sources})
target_link_libraries(maintenance_demo PRIVATE RulesForge)
cmake_configure_target(maintenance_demo FOLDER examples/maintenance)

set(maintenance_bundle_dir ${CMAKE_CURRENT_BINARY_DIR}/bundle)
file(MAKE_DIRECTORY ${maintenance_bundle_dir})
foreach(name maintenance.schema maintenance.rfl signal.toad decision.toad)
    configure_file(${name} ${maintenance_bundle_dir}/${name} COPYONLY)
endforeach()
foreach(bundle bundle bundle-conservative)
    if(bundle STREQUAL "bundle")
        set(MAINTENANCE_DECISION_EXPORT decide)
    else()
        set(MAINTENANCE_DECISION_EXPORT decide_conservative)
    endif()
    configure_file(
        bundle.json.in ${maintenance_bundle_dir}/${bundle}.json.in @ONLY
    )
    file(
        GENERATE
        OUTPUT ${maintenance_bundle_dir}/${bundle}.json
        INPUT ${maintenance_bundle_dir}/${bundle}.json.in
    )
endforeach()
set(maintenance_provider
    ${maintenance_bundle_dir}/${CMAKE_SHARED_MODULE_PREFIX}rulesforge_toad${CMAKE_SHARED_MODULE_SUFFIX}
)
set(maintenance_math_plugin
    ${maintenance_bundle_dir}/${CMAKE_SHARED_MODULE_PREFIX}maintenance_math${CMAKE_SHARED_MODULE_SUFFIX}
)
set(maintenance_runtime_copy)
if(WIN32)
    list(
        APPEND
        maintenance_runtime_copy
        COMMAND
        ${CMAKE_COMMAND}
        -E
        copy_if_different
        $<TARGET_RUNTIME_DLLS:rulesforge_toad>
        ${maintenance_bundle_dir}
    )
endif()
add_custom_command(
    OUTPUT ${maintenance_provider} ${maintenance_math_plugin}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rulesforge_toad>
            ${maintenance_bundle_dir}
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:maintenance_math>
        ${maintenance_bundle_dir} ${maintenance_runtime_copy}
    DEPENDS rulesforge_toad $<TARGET_FILE:rulesforge_toad> maintenance_math
            $<TARGET_FILE:maintenance_math>
    COMMAND_EXPAND_LISTS
)
add_custom_target(
    maintenance_bundle DEPENDS ${maintenance_provider}
                               ${maintenance_math_plugin}
)
add_dependencies(maintenance_demo maintenance_bundle)
