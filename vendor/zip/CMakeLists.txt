set(Z_TARGET zip)
add_library(${Z_TARGET} OBJECT zip.c zip.h miniz.h)
target_include_directories(${Z_TARGET} PUBLIC .)
set_target_properties(${Z_TARGET} PROPERTIES FOLDER shared/${Z_TARGET})
