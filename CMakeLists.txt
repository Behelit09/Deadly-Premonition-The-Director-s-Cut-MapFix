cmake_minimum_required(VERSION 3.15)
project(MapFix C)

set(CMAKE_C_STANDARD 11)

if(NOT WIN32)
    message(FATAL_ERROR "MapFix is only intended for Windows (Deadly Premonition PC).")
endif()

# MapFix must be built as a 32-bit DLL (.asi)
add_library(MapFix SHARED
    src/MapFix.c
)

set_target_properties(MapFix PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "MapFix"
)

target_compile_definitions(MapFix PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(MapFix PRIVATE
    kernel32
    user32
)
