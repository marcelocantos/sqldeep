# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.18)
project(sqldeep C CXX)

# Paths relative to the repo root
set(SQLDEEP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
set(DP_SRC ${SQLDEEP_ROOT}/vendor/deepparser/src)

# C++20 for sqldeep.cpp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Deepparser (vendored liteparser fork) — five C TUs bundled into libsqldeep
# by cvfile; JNI must compile the same set from a fresh checkout.
set(DEEPPARSER_SOURCES
    ${DP_SRC}/arena.c
    ${DP_SRC}/liteparser.c
    ${DP_SRC}/lp_tokenize.c
    ${DP_SRC}/lp_unparse.c
    ${DP_SRC}/parse.c
)

add_library(sqldeep SHARED
    sqldeep_jni.c
    ${SQLDEEP_ROOT}/dist/sqldeep_xml.c
    ${SQLDEEP_ROOT}/dist/sqldeep.cpp
    ${DEEPPARSER_SOURCES}
    ${SQLDEEP_ROOT}/vendor/src/sqlite3.c
)

target_include_directories(sqldeep PRIVATE
    ${SQLDEEP_ROOT}/dist
    ${DP_SRC}
    ${SQLDEEP_ROOT}/vendor/include
)

# Desktop / CI host builds need jni.h from a JDK; the Android NDK supplies it.
if(NOT ANDROID)
    find_package(JNI REQUIRED)
    target_include_directories(sqldeep PRIVATE ${JNI_INCLUDE_DIRS})
endif()

# Match cvfile deepparser flags (suppress noisy upstream warnings).
set_source_files_properties(${DEEPPARSER_SOURCES} PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare -DNDEBUG"
)

# Android log library (not available on desktop — conditional).
find_library(log-lib log)
if(log-lib)
    target_link_libraries(sqldeep ${log-lib})
endif()
