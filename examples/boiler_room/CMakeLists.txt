foreach(module physics statistics scoring)
    cmake_add_source(boiler_${module}_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR}/native
        PATTERNS "${module}.cpp")
    add_library(boiler_${module} MODULE ${boiler_${module}_sources})
    target_link_libraries(boiler_${module} PRIVATE Toad::PluginABI)
    cmake_configure_target(boiler_${module} FOLDER examples/boiler_room)
endforeach()

salts_idl_target(TARGET boiler_room_schema IDL boiler.schema
    ARTIFACTS MESSAGE BINARY_CODEC FOLDER examples/boiler_room)
cmake_add_source(boiler_simulation_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    PATTERNS "simulation.cpp" "runtime.cpp" "text.cpp" "*.hpp" "*.re")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/labels.cpp
    COMMAND ${RE2C_EXECUTABLE} -W --no-debug-info -o ${CMAKE_CURRENT_BINARY_DIR}/labels.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/labels.re
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/labels.re ${RE2C_EXECUTABLE}
    COMMENT "Generating boiler-room label lexer with re2c" VERBATIM)
add_library(boiler_room_simulation STATIC ${boiler_simulation_sources}
    ${CMAKE_CURRENT_BINARY_DIR}/labels.cpp)
target_include_directories(boiler_room_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(boiler_room_simulation PUBLIC RulesForge Salts::Core Threads::Threads
    PRIVATE boiler_room_schema_native Salts::DataBind Salts::SIMD)
cmake_configure_target(boiler_room_simulation FOLDER examples/boiler_room)
cmake_add_source(boiler_demo_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "main.cpp")
add_executable(boiler_room_demo ${boiler_demo_sources})
target_link_libraries(boiler_room_demo PRIVATE boiler_room_simulation)
cmake_configure_target(boiler_room_demo FOLDER examples/boiler_room)

set(boiler_bundle_dir ${CMAKE_CURRENT_BINARY_DIR}/bundle)
file(MAKE_DIRECTORY ${boiler_bundle_dir})
foreach(name boiler.schema boiler.rfl)
    configure_file(${name} ${boiler_bundle_dir}/${name} COPYONLY)
endforeach()
foreach(name thermal pump balance alert dispatch)
    configure_file(scripts/${name}.toad ${boiler_bundle_dir}/${name}.toad COPYONLY)
endforeach()
file(GENERATE OUTPUT ${boiler_bundle_dir}/bundle.json
    INPUT ${CMAKE_CURRENT_SOURCE_DIR}/bundle.json.in)
set(boiler_bundle_files)
set(boiler_bundle_commands)
set(boiler_bundle_dependencies)
foreach(target rulesforge_toad boiler_physics boiler_statistics boiler_scoring)
    list(APPEND boiler_bundle_files ${boiler_bundle_dir}/${CMAKE_SHARED_MODULE_PREFIX}${target}${CMAKE_SHARED_MODULE_SUFFIX})
    list(APPEND boiler_bundle_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${target}> ${boiler_bundle_dir})
    list(APPEND boiler_bundle_dependencies ${target} $<TARGET_FILE:${target}>)
endforeach()
if(WIN32)
    list(APPEND boiler_bundle_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:rulesforge_toad> ${boiler_bundle_dir})
endif()
add_custom_command(OUTPUT ${boiler_bundle_files} ${boiler_bundle_commands}
    DEPENDS ${boiler_bundle_dependencies} COMMAND_EXPAND_LISTS)
add_custom_target(boiler_room_bundle DEPENDS ${boiler_bundle_files})
add_dependencies(boiler_room_demo boiler_room_bundle)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
