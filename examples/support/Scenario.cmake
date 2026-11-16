# Every domain owns the same deployment shape; schema and business policy stay local.
function(rulesforge_add_scenario name)
    cmake_add_source(scenario_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                     PATTERNS "main.cpp" "input.hpp")
    add_executable(${name}_demo ${scenario_sources})
    target_link_libraries(${name}_demo PRIVATE rulesforge_example_support)
    cmake_configure_target(${name}_demo FOLDER examples/${name})

    set(scenario_bundle_dir ${CMAKE_CURRENT_BINARY_DIR}/bundle)
    file(MAKE_DIRECTORY ${scenario_bundle_dir})
    foreach(asset scenario.schema rules.rfl calculations.toad)
        configure_file(${asset} ${scenario_bundle_dir}/${asset} COPYONLY)
    endforeach()
    foreach(mode interpreter jit)
        set(RULESFORGE_EXAMPLE_MODE ${mode})
        configure_file(bundle.json.in ${scenario_bundle_dir}/bundle-${mode}.json.in @ONLY)
        file(GENERATE OUTPUT ${scenario_bundle_dir}/bundle-${mode}.json
             INPUT ${scenario_bundle_dir}/bundle-${mode}.json.in)
    endforeach()
    set(scenario_provider ${scenario_bundle_dir}/${CMAKE_SHARED_MODULE_PREFIX}rulesforge_toad${CMAKE_SHARED_MODULE_SUFFIX})
    set(runtime_copy)
    if(WIN32)
        list(APPEND runtime_copy COMMAND ${CMAKE_COMMAND} -E copy_if_different
             $<TARGET_RUNTIME_DLLS:rulesforge_toad> ${scenario_bundle_dir})
    endif()
    add_custom_command(OUTPUT ${scenario_provider}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rulesforge_toad> ${scenario_bundle_dir}
        ${runtime_copy}
        DEPENDS rulesforge_toad $<TARGET_FILE:rulesforge_toad>
        COMMAND_EXPAND_LISTS)
    add_custom_target(${name}_bundle DEPENDS ${scenario_provider})
    add_dependencies(${name}_demo ${name}_bundle)
    if(BUILD_TESTS)
        add_subdirectory(tests)
    endif()
endfunction()
