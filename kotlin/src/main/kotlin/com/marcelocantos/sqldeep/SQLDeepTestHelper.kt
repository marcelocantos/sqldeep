// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// JNI test helper: direct SQLite access for integration testing.
// Opens in-memory databases with sqldeep runtime functions pre-registered.

package com.marcelocantos.sqldeep

/**
 * Direct SQLite access for integration tests. Opens in-memory databases
 * with all sqldeep runtime functions (xml_element, sqldeep_json_object, etc.)
 * pre-registered.
 *
 * This bypasses Android's SQLiteDatabase to give tests full control over
 * the native sqlite3* handle.
 */
object SQLDeepTestHelper {
    init {
        System.loadLibrary("sqldeep")
    }

    /**
     * Open an in-memory SQLite database with sqldeep functions registered.
     * @return native sqlite3* handle as a Long
     */
    @JvmStatic
    fun openMemoryDB(): Long = nativeOpenMemoryDB()

    /**
     * Close a database previously opened with [openMemoryDB].
     */
    @JvmStatic
    fun closeDB(dbHandle: Long) = nativeCloseDB(dbHandle)

    /**
     * Execute a SQL statement (DDL, INSERT, etc.) with no result.
     * @throws RuntimeException on SQL error
     */
    @JvmStatic
    fun execSQL(dbHandle: Long, sql: String) = nativeExecSQL(dbHandle, sql)

    /**
     * Execute a query and return all rows (first column only) as strings.
     * NULL values are returned as the string "NULL".
     * @throws RuntimeException on SQL error
     */
    @JvmStatic
    fun queryRows(dbHandle: Long, sql: String): Array<String> =
        nativeQueryRows(dbHandle, sql)

    @JvmStatic
    private external fun nativeOpenMemoryDB(): Long

    @JvmStatic
    private external fun nativeCloseDB(dbHandle: Long)

    @JvmStatic
    private external fun nativeExecSQL(dbHandle: Long, sql: String)

    @JvmStatic
    private external fun nativeQueryRows(dbHandle: Long, sql: String): Array<String>
}
