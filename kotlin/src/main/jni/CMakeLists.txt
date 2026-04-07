cmake_minimum_required(VERSION 3.18)
project(sqldeep C)

# Paths relative to the repo root
set(SQLDEEP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_library(sqldeep SHARED
    sqldeep_jni.c
    ${SQLDEEP_ROOT}/dist/sqldeep_xml.c
)

target_include_directories(sqldeep PRIVATE
    ${SQLDEEP_ROOT}/dist
    ${SQLDEEP_ROOT}/vendor/include
)

# Link against the system SQLite (Android provides it), or against a
# vendored copy if building a custom SQLite (e.g., requery/sqlite-android).
# Uncomment the next line if using the repo's vendored sqlite3.c:
# target_sources(sqldeep PRIVATE ${SQLDEEP_ROOT}/vendor/src/sqlite3.c)
