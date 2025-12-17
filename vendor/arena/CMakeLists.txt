set(TARGET_NAME TurboMemory)

# Memory management library - contains both simple pool and zero-copy arena
add_library(${TARGET_NAME} STATIC
    src/memory_pool.c
    src/arena_buffer.c
)

# Public alias
add_library(TurboNet::Memory ALIAS ${TARGET_NAME})

# Include directories
target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set target properties
set_target_properties(${TARGET_NAME} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "memory"
)

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /W4
        /wd4100  # Suppress unused parameter warnings
    )
endif()

# Export public headers
set(MEMORY_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/memory_pool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/arena_buffer.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
    PUBLIC_HEADER "${MEMORY_PUBLIC_HEADERS}"
    FOLDER "vendor/memory"
)

# Tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
