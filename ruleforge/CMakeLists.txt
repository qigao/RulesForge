set(TARGET_NAME RuleForge)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/rete/*.c*
)
add_library(${TARGET_NAME} STATIC ${SOURCES})
# Find dependencies
find_package(pegtl)
find_package(xxHash CONFIG REQUIRED)
find_package(qjs REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
# Link dependencies
target_link_libraries(
    ${TARGET_NAME}
    PUBLIC
        TurboNet::Utils
    PRIVATE
        hashmap
        qjs glaze::glaze jsoncons::jsoncons
        taocpp::pegtl
         xxHash::xxhash
)
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC /bigobj )
endif()
# Disable debug/info logging in release builds for performance
# TURBO_LOG_LEVEL: DEBUG=0, INFO=1, WARN=2, ERROR=3, FATAL=4, OFF=5
target_compile_definitions(${TARGET_NAME} PUBLIC
    $<$<CONFIG:Release>:TLOG_ACTIVE_LEVEL=2>
    $<$<CONFIG:RelWithDebInfo>:TLOG_ACTIVE_LEVEL=2>
    $<$<CONFIG:MinSizeRel>:TLOG_ACTIVE_LEVEL=2>
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${TARGET_NAME}")
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
# Tests
add_subdirectory(test)
add_subdirectory(example)
add_subdirectory(benchmark)
