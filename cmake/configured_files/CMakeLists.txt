include(gitVersion)

configure_file(version.h.in ${CMAKE_BINARY_DIR}/version/version.h)

include_directories(${CMAKE_BINARY_DIR}/version)
