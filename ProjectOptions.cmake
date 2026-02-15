set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" OFF)

option(ENABLE_GIT_INFO "List git status" OFF)
option(ENABLE_CROSS_COMPILING "Detect cross compiler and setup toolchain" ON)
option(ENABLE_SAMPLES "Enable sample projects" ON)
option(BUILD_SHARED_LIBS "Build shared instead of static libraries." ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)
