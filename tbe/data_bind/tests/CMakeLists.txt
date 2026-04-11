# =============================================================================
# Tests
# =============================================================================

add_executable(test_data_bind test_data_bind.c ../tbe_helpers.cpp)
target_link_libraries(
    test_data_bind PRIVATE data_bind TurboNet::TinyTest rfl_parser
)
set_target_properties(test_data_bind PROPERTIES FOLDER tbe/data_bind)
add_test(NAME test_data_bind COMMAND test_data_bind)

add_executable(test_json_codec test_json_codec.cpp)
target_link_libraries(
    test_json_codec PRIVATE data_bind TurboNet::TinyTest rfl_parser TurboNet::Parser
)
target_include_directories(
    test_json_codec PRIVATE
    ${CMAKE_SOURCE_DIR}/rulesforge/include
    ${CMAKE_SOURCE_DIR}/rulesforge/src
)
set_target_properties(test_json_codec PROPERTIES FOLDER tbe/data_bind)
add_test(NAME test_json_codec COMMAND test_json_codec)

add_executable(test_csv_codec test_csv_codec.cpp)
target_link_libraries(
    test_csv_codec PRIVATE data_bind TurboNet::TinyTest rfl_parser TurboNet::Parser
)
target_include_directories(
    test_csv_codec PRIVATE
    ${CMAKE_SOURCE_DIR}/rulesforge/include
    ${CMAKE_SOURCE_DIR}/rulesforge/src
)
set_target_properties(test_csv_codec PROPERTIES FOLDER tbe/data_bind)
add_test(NAME test_csv_codec COMMAND test_csv_codec)

add_executable(test_binary_codec test_binary_codec.cpp)
target_link_libraries(
    test_binary_codec PRIVATE data_bind TurboNet::TinyTest rfl_parser
)
set_target_properties(test_binary_codec PROPERTIES FOLDER tbe/data_bind)
add_test(NAME test_binary_codec COMMAND test_binary_codec)
