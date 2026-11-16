
# Version information (keep in sync with rules_forge.h).

# Define the target name for the shared library
set(TARGET_NAME RulesForge)

cmake_add_source(toad_provider_sources DIRS ${CMAKE_CURRENT_SOURCE_DIR}/toad)
add_library(rulesforge_toad MODULE ${toad_provider_sources})
target_include_directories(rulesforge_toad PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(rulesforge_toad PRIVATE Toad::Script Salts::DataBind CMeta::Provider CMeta::PluginABI)
cmake_configure_target(rulesforge_toad FOLDER rulesforge/toad)
if(WIN32)
    add_custom_command(TARGET rulesforge_toad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rulesforge_toad>
                $<TARGET_FILE_DIR:rulesforge_toad>
        COMMAND_EXPAND_LISTS)
endif()
install(TARGETS rulesforge_toad LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Add the shared library target
add_library(
    ${TARGET_NAME} SHARED src/rules_forge.cpp rfl_value_bridge.cpp

)

target_compile_definitions(${TARGET_NAME} PRIVATE RULES_FORGE_BUILD)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)
# PRIVATE: Internal implementation headers - not exposed to consumers
target_include_directories(
    ${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(
    ${TARGET_NAME}
    PRIVATE rfl_parser engine
    PUBLIC Salts::Core Salts::DataBind
)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER capi/)

if(WIN32)
    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${TARGET_NAME}>
                $<TARGET_FILE_DIR:${TARGET_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif()


if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
# Set output directory for the shared library
set_target_properties(
    ${TARGET_NAME}
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
               ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
