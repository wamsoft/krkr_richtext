cmake_minimum_required(VERSION 3.31)

add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

project(krkr_richtext)

set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

# richtext ライブラリを参照
set(RICHTEXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../richtext CACHE PATH "Path to richtext library")
add_subdirectory(${RICHTEXT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/richtext)

#------------------------------------------------------------------------------
# 吉里吉里プラグイン（TJS バインディング）
#------------------------------------------------------------------------------

krkrz_plugin(richtext
    NCBIND
    INCLUDES
        ${RICHTEXT_DIR}/include
    SOURCES
        src/main.cpp
        src/EscapeConverter.cpp
    LIBRARIES
        richtext_lib
)
