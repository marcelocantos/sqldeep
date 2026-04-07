// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Data-driven SQLite integration tests for the Kotlin/Android binding.
// Loads test cases from testdata/sqlite.yaml (via androidTest assets),
// transpiles sqldeep syntax, executes against in-memory SQLite, and
// verifies results.

package com.marcelocantos.sqldeep

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.yaml.snakeyaml.Yaml

@RunWith(AndroidJUnit4::class)
class SQLiteIntegrationTest {

    // ── YAML schema ──────────────────────────────────────────────────

    private data class TestCase(
        val name: String,
        val setup: List<String>,
        val input: String?,
        val rawSql: String?,
        val expected: List<String>,
        val expectedContains: List<String>,
        val fks: List<ForeignKey>,
        val steps: List<Step>
    )

    private data class Step(
        val transpileExec: String?,
        val transpileQuery: String?
    )

    // ── YAML loading ─────────────────────────────────────────────────

    private fun loadTests(): List<TestCase> {
        val ctx = InstrumentationRegistry.getInstrumentation().context
        val yaml = Yaml()
        val text = ctx.assets.open("sqlite.yaml").bufferedReader().readText()
        val raw = yaml.load<List<Map<String, Any?>>>(text)
        return raw.map { m ->
            @Suppress("UNCHECKED_CAST")
            val fkMaps = (m["fks"] as? List<Map<String, Any?>>) ?: emptyList()
            val fks = fkMaps.map { fk ->
                @Suppress("UNCHECKED_CAST")
                val cols = (fk["columns"] as? List<Map<String, String>>) ?: emptyList()
                ForeignKey(
                    fromTable = fk["from_table"] as String,
                    toTable = fk["to_table"] as String,
                    columns = cols.map { c ->
                        ColumnPair(
                            fromColumn = c["from_column"]!!,
                            toColumn = c["to_column"]!!
                        )
                    }
                )
            }

            @Suppress("UNCHECKED_CAST")
            val stepMaps = (m["steps"] as? List<Map<String, String?>>) ?: emptyList()
            val steps = stepMaps.map { s ->
                Step(
                    transpileExec = s["transpile_exec"],
                    transpileQuery = s["transpile_query"]
                )
            }

            TestCase(
                name = m["name"] as String,
                setup = @Suppress("UNCHECKED_CAST") (m["setup"] as? List<String>) ?: emptyList(),
                input = m["input"] as? String,
                rawSql = m["raw_sql"] as? String,
                expected = @Suppress("UNCHECKED_CAST") ((m["expected"] as? List<Any?>)?.map { it.toString() } ?: emptyList()),
                expectedContains = @Suppress("UNCHECKED_CAST") ((m["expected_contains"] as? List<Any?>)?.map { it.toString() } ?: emptyList()),
                fks = fks,
                steps = steps
            )
        }
    }

    // ── Test runner ──────────────────────────────────────────────────

    @Test
    fun testAllSQLiteCases() {
        val tests = loadTests()
        val failures = mutableListOf<String>()

        for (tc in tests) {
            var db: Long = 0
            try {
                db = SQLDeepTestHelper.openMemoryDB()

                // Setup
                for (sql in tc.setup) {
                    SQLDeepTestHelper.execSQL(db, sql)
                }

                val rows: Array<String> = when {
                    tc.steps.isNotEmpty() -> {
                        // Multi-step (view recomposition etc.)
                        var lastRows: Array<String> = emptyArray()
                        for (step in tc.steps) {
                            if (step.transpileExec != null) {
                                val sql = SQLDeep.transpile(step.transpileExec)
                                SQLDeepTestHelper.execSQL(db, sql)
                            }
                            if (step.transpileQuery != null) {
                                val sql = SQLDeep.transpile(step.transpileQuery)
                                lastRows = SQLDeepTestHelper.queryRows(db, sql)
                            }
                        }
                        lastRows
                    }

                    tc.rawSql != null -> {
                        SQLDeepTestHelper.queryRows(db, tc.rawSql)
                    }

                    tc.fks.isNotEmpty() -> {
                        val sql = SQLDeep.transpileFK(tc.input!!, tc.fks)
                        SQLDeepTestHelper.queryRows(db, sql)
                    }

                    else -> {
                        val sql = SQLDeep.transpile(tc.input!!)
                        SQLDeepTestHelper.queryRows(db, sql)
                    }
                }

                // Verify
                if (tc.expectedContains.isNotEmpty()) {
                    if (rows.size != 1) {
                        failures.add("${tc.name}: expected 1 row, got ${rows.size}")
                        continue
                    }
                    for (needle in tc.expectedContains) {
                        if (needle !in rows[0]) {
                            failures.add("${tc.name}: missing '$needle' in '${rows[0]}'")
                        }
                    }
                } else {
                    if (rows.size != tc.expected.size) {
                        failures.add("${tc.name}: row count: got ${rows.size}, want ${tc.expected.size}")
                        continue
                    }
                    for (i in tc.expected.indices) {
                        if (rows[i] != tc.expected[i]) {
                            failures.add("${tc.name}: row[$i]: got '${rows[i]}', want '${tc.expected[i]}'")
                        }
                    }
                }
            } catch (e: Exception) {
                failures.add("${tc.name}: exception: ${e.message}")
            } finally {
                if (db != 0L) {
                    SQLDeepTestHelper.closeDB(db)
                }
            }
        }

        if (failures.isNotEmpty()) {
            val msg = buildString {
                appendLine("${failures.size} test(s) failed:")
                for (f in failures) {
                    appendLine("  - $f")
                }
            }
            throw AssertionError(msg)
        }
    }

    // ── Transpiler smoke test ────────────────────────────────────────

    @Test
    fun testTranspileBasic() {
        val sql = SQLDeep.transpile("SELECT { id, name } FROM t")
        assertTrue("should contain sqldeep_json_object", sql.contains("sqldeep_json_object"))
        assertTrue("should contain FROM t", sql.contains("FROM t"))
    }

    @Test
    fun testTranspileError() {
        try {
            SQLDeep.transpile("SELECT {")
            throw AssertionError("expected TranspileException")
        } catch (e: SQLDeep.TranspileException) {
            assertTrue("error should have message", e.msg.isNotEmpty())
        }
    }

    @Test
    fun testVersion() {
        val v = SQLDeep.version()
        assertTrue("version should match semver pattern", v.matches(Regex("\\d+\\.\\d+\\.\\d+")))
    }

    // ── FK-guided transpile test ─────────────────────────────────────

    @Test
    fun testTranspileFK() {
        val input = """
            SELECT {
                name,
                orders: FROM c->orders o ORDER BY o.id SELECT { total },
            } FROM customers c
        """.trimIndent()

        val fks = listOf(
            ForeignKey(
                fromTable = "orders",
                toTable = "customers",
                columns = listOf(ColumnPair(fromColumn = "cust_id", toColumn = "id"))
            )
        )

        val sql = SQLDeep.transpileFK(input, fks)
        assertTrue("FK sql should contain cust_id", sql.contains("cust_id"))
    }

    // ── Runtime registration test ────────────────────────────────────

    @Test
    fun testRuntimeRegistration() {
        val db = SQLDeepTestHelper.openMemoryDB()
        try {
            // Runtime functions are registered at open time; verify they work.
            val rows = SQLDeepTestHelper.queryRows(db,
                "SELECT sqldeep_json_object('a', 1, 'b', 2)")
            assertEquals(1, rows.size)
            assertEquals("""{"a":1,"b":2}""", rows[0])
        } finally {
            SQLDeepTestHelper.closeDB(db)
        }
    }
}
