set(TARGET_NAME drills)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/rete/*.c*
)
add_library(${TARGET_NAME} OBJECT ${SOURCES})
# Find dependencies
find_package(pegtl )
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
target_include_directories( ${TARGET_NAME} PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
find_package(magic_enum CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(qjs REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(mimalloc REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
# Link dependencies
target_link_libraries(
    ${TARGET_NAME}
    PUBLIC
    pubcxx
    hashmap
    mimalloc
    qjs glaze::glaze jsoncons::jsoncons
    taocpp::pegtl
    magic_enum::magic_enum
    xxHash::xxhash
)
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(${TARGET_NAME} PUBLIC /bigobj )
target_compile_definitions(${TARGET_NAME} PUBLIC  HAS_MIMALLOC)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${TARGET_NAME}")
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
# Tests
message("-- Adding tests to ${TARGET_NAME}")
add_subdirectory(test)
message("-- Adding examples to ${TARGET_NAME}")
add_subdirectory(example)
