# =============================================================================
# Library
# =============================================================================

set(TARGET_NAME data_bind)

add_library(
    ${TARGET_NAME} STATIC
    data_bind_multi.cpp
    data_bind.h
    tbe_helpers.cpp
    formats/json/json_codec.cpp
    formats/json/json_codec.h
    formats/csv/csv_codec.cpp
    formats/csv/csv_codec.h
    formats/binary/binary_codec.cpp
    formats/binary/binary_codec.h
    formats/binary/mir_codegen.cpp
    formats/binary/mir_codegen.h
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE ${CMAKE_SOURCE_DIR}/tbe/mir
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/rulesforge/include
            ${CMAKE_SOURCE_DIR}/parser/include
)

target_link_libraries(
    ${TARGET_NAME}
    PRIVATE mir_static
    PUBLIC TurboNet::Utils TurboNet::Parser
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER tbe/data_bind)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
