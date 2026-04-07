// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Desktop smoke test: loads the native library and runs basic checks.
// Run with: kotlinc desktop_smoke_test.kt -include-runtime -d test.jar && java -Djava.library.path=/tmp/sqldeep-kotlin-build -jar test.jar
//
// Or from the kotlin/ directory:
//   kotlinc desktop_smoke_test.kt src/main/kotlin/com/marcelocantos/sqldeep/SQLDeep.kt src/main/kotlin/com/marcelocantos/sqldeep/SQLDeepRuntime.kt src/main/kotlin/com/marcelocantos/sqldeep/SQLDeepTestHelper.kt -include-runtime -d test.jar
//   java -Djava.library.path=/tmp/sqldeep-kotlin-build -jar test.jar

@file:Suppress("UNCHECKED_CAST")

package com.marcelocantos.sqldeep

fun main() {
    System.loadLibrary("sqldeep")
    var passed = 0
    var failed = 0

    fun check(name: String, got: String, want: String) {
        if (got == want) {
            passed++
        } else {
            failed++
            System.err.println("FAIL: $name\n  got  = $got\n  want = $want")
        }
    }

    // Test transpiler
    val sql = SQLDeep.transpile("SELECT { id, name } FROM t")
    check("transpile basic",
        sql,
        "SELECT sqldeep_json_object('id', id, 'name', name) FROM t")

    // Test transpile error
    try {
        SQLDeep.transpile("SELECT { FROM")
        failed++
        System.err.println("FAIL: expected transpile error")
    } catch (e: SQLDeep.TranspileException) {
        passed++
    }

    // Test runtime: open DB, create table, insert, transpile+query
    val db = SQLDeepTestHelper.openMemoryDB()
    SQLDeepTestHelper.execSQL(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)")
    SQLDeepTestHelper.execSQL(db, "INSERT INTO t VALUES(1, 'alice')")
    SQLDeepTestHelper.execSQL(db, "INSERT INTO t VALUES(2, 'bob')")

    val deepSQL = "SELECT { id, name } FROM t ORDER BY id"
    val transpiled = SQLDeep.transpile(deepSQL)
    val rows = SQLDeepTestHelper.queryRows(db, transpiled)
    check("runtime row count", rows.size.toString(), "2")
    if (rows.size == 2) {
        check("runtime row 0", rows[0], """{"id":1,"name":"alice"}""")
        check("runtime row 1", rows[1], """{"id":2,"name":"bob"}""")
    }

    // Test nested subquery
    SQLDeepTestHelper.execSQL(db, "CREATE TABLE orders(id INTEGER, cid INTEGER, total REAL)")
    SQLDeepTestHelper.execSQL(db, "INSERT INTO orders VALUES(10, 1, 99.5)")
    SQLDeepTestHelper.execSQL(db, "INSERT INTO orders VALUES(11, 1, 42.0)")
    val nested = SQLDeep.transpile("""
        SELECT { id, name,
            orders: SELECT { order_id: id, total }
                FROM orders WHERE cid = c.id ORDER BY id,
        } FROM t c WHERE c.id = 1
    """.trimIndent())
    val nestedRows = SQLDeepTestHelper.queryRows(db, nested)
    check("nested row count", nestedRows.size.toString(), "1")
    if (nestedRows.isNotEmpty()) {
        check("nested result", nestedRows[0],
            """{"id":1,"name":"alice","orders":[{"order_id":10,"total":99.5},{"order_id":11,"total":42.0}]}""")
    }

    // Test JSON booleans
    val boolSQL = SQLDeep.transpile("SELECT { id, active: true } FROM t WHERE id = 1")
    val boolRows = SQLDeepTestHelper.queryRows(db, boolSQL)
    check("json boolean", boolRows.firstOrNull() ?: "",
        """{"id":1,"active":true}""")

    // Test XML
    val xmlSQL = SQLDeep.transpile("""SELECT <div class="card">{name}</div> FROM t WHERE id = 1""")
    val xmlRows = SQLDeepTestHelper.queryRows(db, xmlSQL)
    check("xml element", xmlRows.firstOrNull() ?: "",
        """<div class="card">alice</div>""")

    // Test JSONML
    val jsonmlSQL = SQLDeep.transpile("SELECT jsonml(<span>{name}</span>) FROM t WHERE id = 1")
    val jsonmlRows = SQLDeepTestHelper.queryRows(db, jsonmlSQL)
    check("jsonml", jsonmlRows.firstOrNull() ?: "",
        """["span","alice"]""")

    // Test qualified bare field
    SQLDeepTestHelper.execSQL(db, "CREATE TABLE s(id INTEGER, repo TEXT)")
    SQLDeepTestHelper.execSQL(db, "INSERT INTO s VALUES(1, 'sqldeep')")
    val qualSQL = SQLDeep.transpile("SELECT { t.id, s.repo } FROM t JOIN s ON s.id = t.id")
    val qualRows = SQLDeepTestHelper.queryRows(db, qualSQL)
    check("qualified bare field", qualRows.firstOrNull() ?: "",
        """{"id":1,"repo":"sqldeep"}""")

    SQLDeepTestHelper.closeDB(db)

    println("$passed passed, $failed failed")
    if (failed > 0) {
        System.exit(1)
    }
}
