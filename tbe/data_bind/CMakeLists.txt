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
    formats/binary/binary_codec.cpp
    formats/binary/binary_codec.h
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE 
            ${CMAKE_SOURCE_DIR}/tbe/mir
            ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    ${TARGET_NAME}
    PRIVATE mir_static rfl_parser TurboNet::Parser
    PUBLIC TurboNet::Utils
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER tbe/data_bind)

add_subdirectory(tests)