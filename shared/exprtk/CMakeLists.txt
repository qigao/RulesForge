# **************************************************************
# * C++ Mathematical Expression Toolkit Library        *
# * *
# * CMake Build Configuration                                  *
# * URL: https://www.partow.net/programming/exprtk/index.html  *
# * *
# * SPDX-License-Identifier: MIT                               *
# **************************************************************

cmake_minimum_required(VERSION 3.16)
project(exprtk LANGUAGES CXX)

# Header-only library
add_library(exprtk INTERFACE)
add_library(exprtk::exprtk ALIAS exprtk)

target_include_directories(
    exprtk INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                     $<INSTALL_INTERFACE:include>
)

target_compile_features(exprtk INTERFACE cxx_std_11)

add_subdirectory(examples)

# Tests
enable_testing()
add_subdirectory(tests)
