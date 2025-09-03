# For more information about using CMake with Android NDK,
# see https://developer.android.com/studio/projects/configure-cmake

cmake_minimum_required(VERSION 3.22.1 FATAL_ERROR)

project("native-lib")

# Find the drills_capi library. Assuming it's prebuilt and placed in jniLibs.
# You might need to adjust the path or use find_library if it's in a system path.
# For simplicity, we assume it's found by the build system or placed correctly.
# If you place libdrills_capi.so in app/src/main/jniLibs/<abi>/, Android Studio
# will automatically pick it up and link it.

# Add the drills_capi include directory if needed
# For example: include_directories("path/to/drills/capi/include")
# Assuming drills_capi.h is available in the NDK include path or copied to this project.

# Create a static library for your JNI code
add_library(
    native-lib
    SHARED
    native-lib.cpp
)

# Link the drills_capi library
# The Android build system will automatically find libdrills_capi.so if it's in jniLibs
# or if you've configured your build to find it.
# We just need to tell CMake that our native-lib depends on it.

# If drills_capi is a prebuilt library, you might need to explicitly add it like this:
# add_library(drills_capi_prebuilt SHARED IMPORTED)
# set_target_properties(drills_capi_prebuilt PROPERTIES IMPORTED_LOCATION
#                       ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libdrills_capi.so)
# target_link_libraries(native-lib drills_capi_prebuilt)

# For simplicity, assuming the Android build system handles linking of prebuilt .so files
# placed in jniLibs, we just link against the library name.

target_link_libraries(
    native-lib
    log
    # Link against the drills_capi library. The 'lib' prefix and '.so' suffix are implicit.
    drills_capi
)
