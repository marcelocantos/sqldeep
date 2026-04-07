# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.18)
project(sqldeep C CXX)

# Paths relative to the repo root
set(SQLDEEP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

# C++20 for sqldeep.cpp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(sqldeep SHARED
    sqldeep_jni.c
    ${SQLDEEP_ROOT}/dist/sqldeep_xml.c
    ${SQLDEEP_ROOT}/dist/sqldeep.cpp
    ${SQLDEEP_ROOT}/vendor/src/sqlite3.c
)

target_include_directories(sqldeep PRIVATE
    ${SQLDEEP_ROOT}/dist
    ${SQLDEEP_ROOT}/vendor/include
)

# Android log library (not available on desktop — conditional).
find_library(log-lib log)
if(log-lib)
    target_link_libraries(sqldeep ${log-lib})
endif()
