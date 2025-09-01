set(TARGET_NAME moon)
set(folder_name shared/lua/${TARGET_NAME})
add_library(
    ${TARGET_NAME} OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/include/sol_bind.hpp
)
target_link_libraries(
    ${TARGET_NAME} PUBLIC
)
find_package(sol2)
target_link_libraries(${TARGET_NAME} PUBLIC Lua::Lua sol2::sol2 pubcxx)
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER ${folder_name})
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
