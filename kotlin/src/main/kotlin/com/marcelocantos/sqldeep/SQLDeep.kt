// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// SQLDeep transpiler: converts JSON5-like SQL syntax to standard SQL.

package com.marcelocantos.sqldeep

/**
 * Transpiler for sqldeep's JSON5-like SQL syntax.
 *
 * Converts input like `SELECT { id, name } FROM t` to standard SQL with
 * JSON function calls appropriate for the target database backend.
 *
 * Usage:
 * ```kotlin
 * val sql = SQLDeep.transpile("SELECT { id, name } FROM t")
 * // -> "SELECT sqldeep_json_object('id', id, 'name', name) FROM t"
 * ```
 */
object SQLDeep {
    init {
        System.loadLibrary("sqldeep")
    }

    /**
     * Transpile sqldeep input to standard SQL (SQLite backend).
     *
     * @param input sqldeep syntax
     * @return standard SQL string
     * @throws TranspileException on parse or transpilation errors
     */
    @JvmStatic
    fun transpile(input: String): String {
        return nativeTranspile(input)
            ?: throw IllegalStateException("nativeTranspile returned null without exception")
    }

    /**
     * Transpile with explicit foreign key metadata (SQLite backend).
     *
     * @param input sqldeep syntax
     * @param foreignKeys FK relationships for join resolution
     * @return standard SQL string
     * @throws TranspileException on parse or transpilation errors
     */
    @JvmStatic
    fun transpileFK(input: String, foreignKeys: List<ForeignKey>): String {
        val fromTables = Array(foreignKeys.size) { foreignKeys[it].fromTable }
        val toTables = Array(foreignKeys.size) { foreignKeys[it].toTable }
        val fromColArrays = Array(foreignKeys.size) { i ->
            Array(foreignKeys[i].columns.size) { j -> foreignKeys[i].columns[j].fromColumn }
        }
        val toColArrays = Array(foreignKeys.size) { i ->
            Array(foreignKeys[i].columns.size) { j -> foreignKeys[i].columns[j].toColumn }
        }
        return nativeTranspileFK(input, fromTables, toTables, fromColArrays, toColArrays)
            ?: throw IllegalStateException("nativeTranspileFK returned null without exception")
    }

    /**
     * Library version string (e.g., "0.19.0").
     */
    @JvmStatic
    fun version(): String = nativeVersion()

    @JvmStatic
    private external fun nativeTranspile(input: String): String?

    @JvmStatic
    private external fun nativeTranspileFK(
        input: String,
        fromTables: Array<String>,
        toTables: Array<String>,
        fromColArrays: Array<Array<String>>,
        toColArrays: Array<Array<String>>
    ): String?

    @JvmStatic
    private external fun nativeVersion(): String

    /**
     * Exception thrown when transpilation fails.
     *
     * @property msg error description
     * @property line 1-based line number of the error (0 if unknown)
     * @property col 1-based column number of the error (0 if unknown)
     */
    class TranspileException(
        val msg: String,
        val line: Int,
        val col: Int
    ) : RuntimeException("$msg (line $line, col $col)")
}

/**
 * Foreign key relationship descriptor for FK-guided transpilation.
 */
data class ForeignKey(
    val fromTable: String,
    val toTable: String,
    val columns: List<ColumnPair>
)

/**
 * A single column pair in a foreign key relationship.
 */
data class ColumnPair(
    val fromColumn: String,
    val toColumn: String
)
