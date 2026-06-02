cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
    project(dummy)
endif()

set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

# richtext ライブラリを参照
add_subdirectory(richtext)

#------------------------------------------------------------------------------
# 吉里吉里プラグイン（TJS バインディング）
#------------------------------------------------------------------------------

krkrz_plugin(krkr_richtext
    NCBIND
    SOURCES
        src/main.cpp
        src/EscapeConverter.cpp
    LIBRARIES
        richtext_lib
)
