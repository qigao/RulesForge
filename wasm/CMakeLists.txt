cmake_minimum_required(VERSION 3.15)
project(drills_wasm)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the main Drills project
find_package(drills REQUIRED)

# Add WebAssembly-specific compiler flags
if(EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_FUNCTIONS=['_malloc','_free']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --bind")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s MODULARIZE=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORT_NAME='DrillsEngine'")
endif()

# Create WebAssembly module
add_executable(drills_wasm
    src/drills_wasm_bindings.cpp
)

# Link against the CAPI library
target_link_libraries(drills_wasm PRIVATE drills::capi)

# Set output properties for WebAssembly
if(EMSCRIPTEN)
    set_target_properties(drills_wasm PROPERTIES
        SUFFIX ".js"
        OUTPUT_NAME "drills"
    )
endif()

# Installation
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/drills.js
    ${CMAKE_CURRENT_BINARY_DIR}/drills.wasm
    DESTINATION "."
)
