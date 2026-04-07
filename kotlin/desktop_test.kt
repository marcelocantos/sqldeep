// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Desktop test runner: loads testdata/sqlite.yaml and runs all 79 test cases.
//
// Build:
//   kotlinc desktop_test.kt \
//     src/main/kotlin/com/marcelocantos/sqldeep/SQLDeep.kt \
//     src/main/kotlin/com/marcelocantos/sqldeep/SQLDeepRuntime.kt \
//     src/main/kotlin/com/marcelocantos/sqldeep/SQLDeepTestHelper.kt \
//     -classpath /path/to/snakeyaml-2.3.jar \
//     -include-runtime -d /tmp/sqldeep-kotlin-test.jar
//
// Run:
//   java -Djava.library.path=/tmp/sqldeep-kotlin-build \
//     -classpath /tmp/sqldeep-kotlin-test.jar:/path/to/snakeyaml-2.3.jar \
//     com.marcelocantos.sqldeep.DesktopTestKt

package com.marcelocantos.sqldeep

import org.yaml.snakeyaml.Yaml
import java.io.File

fun main() {
    System.loadLibrary("sqldeep")

    val yamlFile = File("../testdata/sqlite.yaml")
    require(yamlFile.exists()) { "Cannot find ${yamlFile.absolutePath}" }

    val yaml = Yaml()
    val tests = yaml.load<List<Map<String, Any>>>(yamlFile.reader())

    var passed = 0
    var failed = 0

    for (tc in tests) {
        val name = tc["name"] as String
        val setup = (tc["setup"] as? List<*>)?.map { it.toString() } ?: emptyList()
        val input = tc["input"] as? String
        val rawSQL = tc["raw_sql"] as? String
        val expected = (tc["expected"] as? List<*>)?.map { it.toString() }
        val expectedContains = (tc["expected_contains"] as? List<*>)?.map { it.toString() }
        val fks = tc["fks"] as? List<*>
        val steps = tc["steps"] as? List<*>

        val db = SQLDeepTestHelper.openMemoryDB()
        try {
            // Setup
            for (sql in setup) {
                SQLDeepTestHelper.execSQL(db, sql)
            }

            // Execute
            val rows: Array<String> = when {
                steps != null -> {
                    var result: Array<String> = emptyArray()
                    for (step in steps) {
                        val s = step as Map<*, *>
                        val transpileExec = s["transpile_exec"] as? String
                        val transpileQuery = s["transpile_query"] as? String
                        if (transpileExec != null) {
                            val sql = transpileInput(transpileExec, fks)
                            SQLDeepTestHelper.execSQL(db, sql)
                        }
                        if (transpileQuery != null) {
                            val sql = transpileInput(transpileQuery, fks)
                            result = SQLDeepTestHelper.queryRows(db, sql)
                        }
                    }
                    result
                }
                rawSQL != null -> {
                    SQLDeepTestHelper.queryRows(db, rawSQL)
                }
                input != null -> {
                    val sql = transpileInput(input, fks)
                    SQLDeepTestHelper.queryRows(db, sql)
                }
                else -> {
                    System.err.println("SKIP: $name (no input/raw_sql/steps)")
                    continue
                }
            }

            // Verify
            var ok = true
            if (expectedContains != null) {
                if (rows.size != 1) {
                    System.err.println("FAIL: $name — expected 1 row, got ${rows.size}")
                    ok = false
                } else {
                    for (needle in expectedContains) {
                        if (needle !in rows[0]) {
                            System.err.println("FAIL: $name — missing \"$needle\" in ${rows[0]}")
                            ok = false
                        }
                    }
                }
            } else if (expected != null) {
                if (rows.size != expected.size) {
                    System.err.println("FAIL: $name — row count: got ${rows.size}, want ${expected.size}")
                    ok = false
                } else {
                    for (i in expected.indices) {
                        if (rows[i] != expected[i]) {
                            System.err.println("FAIL: $name row[$i]\n  got  = ${rows[i]}\n  want = ${expected[i]}")
                            ok = false
                        }
                    }
                }
            }

            if (ok) passed++ else failed++
        } catch (e: Exception) {
            System.err.println("FAIL: $name — ${e.javaClass.simpleName}: ${e.message}")
            failed++
        } finally {
            SQLDeepTestHelper.closeDB(db)
        }
    }

    println("$passed passed, $failed failed (${passed + failed} total)")
    if (failed > 0) System.exit(1)
}

private fun transpileInput(input: String, fks: List<*>?): String {
    if (fks == null || fks.isEmpty()) return SQLDeep.transpile(input)

    val fkList = fks.map { fk ->
        val m = fk as Map<*, *>
        val cols = (m["columns"] as List<*>).map { c ->
            val cm = c as Map<*, *>
            ColumnPair(cm["from_column"].toString(), cm["to_column"].toString())
        }
        ForeignKey(m["from_table"].toString(), m["to_table"].toString(), cols)
    }
    return SQLDeep.transpileFK(input, fkList)
}
