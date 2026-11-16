cmake_minimum_required(VERSION 3.25)
project(
    RulesForge
    VERSION 0.1.0
    DESCRIPTION "A Rete Network based rule engine"
    HOMEPAGE_URL "https://github.com/qigao/RulesForge"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # All project DSOs, including providers and generated schema modules, must
    # satisfy the SDK's 16 KiB LOAD alignment contract with NDK r27 as well.
    add_link_options("LINKER:-z,max-page-size=16384" "LINKER:-z,common-page-size=16384")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CmakeUtils)
include(Sanitizers)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(BUILD_TESTS OR BUILD_BENCHMARK)
    enable_testing()
endif()

list(PREPEND CMAKE_PREFIX_PATH
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$ENV{VCPKG_CONFIGURATION_SUBDIR}"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_package(Threads REQUIRED)
find_package(MIR CONFIG REQUIRED)
find_package(SaltsBase CONFIG REQUIRED PATHS "$ENV{SALTS_BASE_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(CMeta CONFIG REQUIRED PATHS "$ENV{CMETA_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(SaltsCNet CONFIG REQUIRED PATHS "$ENV{SALTS_CORE_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(Toad 0.1.3 CONFIG REQUIRED PATHS "$ENV{TOAD_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
include(FindTools)
add_subdirectory(core)
add_subdirectory(rulesforge)
add_subdirectory(examples)

install(TARGETS RulesForge EXPORT RulesForgeTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY rulesforge/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT RulesForgeTargets NAMESPACE RulesForge::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RulesForge)
configure_package_config_file(cmake/RulesForgeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RulesForge)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/RulesForgeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RulesForge)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/RulesForge)
