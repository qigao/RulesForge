cmake_minimum_required(VERSION 3.20)
project(RulesForgePackageConsumer LANGUAGES C)

if(NOT DEFINED ENV{RULES_FORGE_ROOT} OR "$ENV{RULES_FORGE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "RULES_FORGE_ROOT is required")
endif()

find_package(RulesForge 0.9 CONFIG REQUIRED
  PATHS "$ENV{RULES_FORGE_ROOT}"
  NO_DEFAULT_PATH)

if(NOT TARGET RulesForge::RulesForge)
  message(FATAL_ERROR "RulesForge::RulesForge is missing")
endif()
if(NOT TARGET DataBind::Runtime)
  message(FATAL_ERROR "RulesForge package did not resolve DataBind::Runtime")
endif()
foreach(legacy_target IN ITEMS
    Salts::DataBind Salts::DataBindCore Salts::DataBindCMeta
    Salts::DataBindCFlow Salts::TbeSchema)
  if(TARGET ${legacy_target})
    message(FATAL_ERROR
      "RulesForge package exposed legacy DataBind ownership: ${legacy_target}")
  endif()
endforeach()

add_executable(rulesforge_package_consumer main.c)
target_link_libraries(rulesforge_package_consumer PRIVATE RulesForge::RulesForge)
