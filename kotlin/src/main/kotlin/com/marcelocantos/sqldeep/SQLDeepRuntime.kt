// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// SQLDeep runtime function registration for Android/Kotlin.
//
// Registers all sqldeep SQLite functions (xml_element, xml_attrs, xml_agg,
// JSONML/JSX variants, and custom JSON functions) on a SQLite connection.
//
// Requires: the native library "sqldeep" compiled from sqldeep_xml.c,
// sqldeep_jni.c, and sqlite3.c (or linked against the app's SQLite).
//
// Usage with requery/sqlite-android (recommended — exposes native handle):
//
//   val db = SQLiteDatabase.openOrCreateDatabase(":memory:", null)
//   SQLDeepRuntime.register(db)
//
// Usage with Android's stock SQLiteDatabase (requires reflection):
//
//   val db = SQLiteDatabase.openOrCreateDatabase(":memory:", null)
//   SQLDeepRuntime.register(db)
//
// The register() overload for android.database.sqlite.SQLiteDatabase uses
// reflection to access the native handle. This works on current Android
// versions but is not guaranteed across all API levels.

package com.marcelocantos.sqldeep

/**
 * Registers all sqldeep runtime functions on a SQLite database connection.
 *
 * All structured values (XML, JSONML, JSX, JSON) use a pure BLOB protocol:
 * - BLOBs starting with '<' are XML markup
 * - BLOBs not starting with '<' are JSON (inline raw)
 *
 * Functions registered:
 * - xml_element, xml_attrs, xml_agg (XML mode)
 * - xml_element_jsonml, xml_attrs_jsonml, jsonml_agg (JSONML mode)
 * - xml_element_jsx, xml_attrs_jsx, jsx_agg (JSX mode)
 * - sqldeep_json, sqldeep_json_object, sqldeep_json_array, sqldeep_json_group_array
 */
object SQLDeepRuntime {
    init {
        System.loadLibrary("sqldeep")
    }

    /**
     * Register sqldeep functions using a raw sqlite3* handle (as a long).
     * Use this with SQLite libraries that expose the native pointer
     * (e.g., requery/sqlite-android's `database.ptr`).
     *
     * @param dbHandle the sqlite3* pointer as a long
     * @return 0 (SQLITE_OK) on success
     */
    @JvmStatic
    fun register(dbHandle: Long): Int {
        return nativeRegister(dbHandle)
    }

    @JvmStatic
    private external fun nativeRegister(dbHandle: Long): Int
}
