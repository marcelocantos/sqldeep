// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Data-driven integration tests: load test cases from testdata/sqlite.yaml,
// transpile sqldeep syntax, execute against an in-memory SQLite database,
// and verify output.

#include <doctest.h>
#include <fkYAML/node.hpp>
#include <sqlite3.h>
#include "sqldeep.h"
#include "sqldeep_xml.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ── Helpers ────────────────────────────────────────────────────────

struct Db {
    sqlite3* db = nullptr;
    Db() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            throw std::runtime_error("failed to open :memory: db");
        sqldeep_register_sqlite(db);
    }
    ~Db() { if (db) sqlite3_close(db); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
};

void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("exec: " + msg + "\nSQL: " + sql);
    }
}

std::vector<std::string> query(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(
            std::string("prepare: ") + sqlite3_errmsg(db) + "\nSQL: " + sql);
    std::vector<std::string> rows;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rows.push_back(text ? text : "NULL");
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
        throw std::runtime_error(std::string("step: ") + sqlite3_errmsg(db));
    return rows;
}

std::string transpile_str(const std::string& input) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* result = sqldeep_transpile(input.c_str(), &err_msg, &err_line, &err_col);
    if (!result) {
        std::string msg = err_msg ? err_msg : "unknown error";
        if (err_msg) sqldeep_free(err_msg);
        throw std::runtime_error("transpile: " + msg);
    }
    std::string sql(result);
    sqldeep_free(result);
    return sql;
}

std::string transpile_fk_str(const std::string& input,
                              const sqldeep_foreign_key* fks, int fk_count) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* result = sqldeep_transpile_fk(input.c_str(), fks, fk_count,
                                         &err_msg, &err_line, &err_col);
    if (!result) {
        std::string msg = err_msg ? err_msg : "unknown error";
        if (err_msg) sqldeep_free(err_msg);
        throw std::runtime_error("transpile_fk: " + msg);
    }
    std::string sql(result);
    sqldeep_free(result);
    return sql;
}

// ── YAML loading ──────────────────────────────────────────────────

struct TestCase {
    std::string name;
    std::vector<std::string> setup;
    std::string input;       // transpile + query
    std::string raw_sql;     // execute directly (no transpile)
    std::vector<std::string> expected;
    std::vector<std::string> expected_contains;

    // FK metadata
    struct FK {
        std::string from_table, to_table;
        std::vector<std::pair<std::string, std::string>> columns;
    };
    std::vector<FK> fks;

    // Multi-step (view recomposition etc.)
    struct Step {
        std::string transpile_exec;
        std::string transpile_query;
    };
    std::vector<Step> steps;
};

std::vector<TestCase> load_tests() {
    std::ifstream f("testdata/sqlite.yaml");
    if (!f) throw std::runtime_error("cannot open testdata/sqlite.yaml");
    auto root = fkyaml::node::deserialize(f);
    std::vector<TestCase> tests;

    for (auto& node : root) {
        TestCase tc;
        tc.name = node["name"].get_value<std::string>();

        if (node.contains("setup"))
            for (auto& s : node["setup"])
                tc.setup.push_back(s.get_value<std::string>());

        if (node.contains("input"))
            tc.input = node["input"].get_value<std::string>();
        if (node.contains("raw_sql"))
            tc.raw_sql = node["raw_sql"].get_value<std::string>();

        if (node.contains("expected"))
            for (auto& e : node["expected"])
                tc.expected.push_back(e.get_value<std::string>());
        if (node.contains("expected_contains"))
            for (auto& e : node["expected_contains"])
                tc.expected_contains.push_back(e.get_value<std::string>());

        if (node.contains("fks")) {
            for (auto& fk_node : node["fks"]) {
                TestCase::FK fk;
                fk.from_table = fk_node["from_table"].get_value<std::string>();
                fk.to_table = fk_node["to_table"].get_value<std::string>();
                for (auto& col : fk_node["columns"]) {
                    fk.columns.emplace_back(
                        col["from_column"].get_value<std::string>(),
                        col["to_column"].get_value<std::string>());
                }
                tc.fks.push_back(std::move(fk));
            }
        }

        if (node.contains("steps")) {
            for (auto& step_node : node["steps"]) {
                TestCase::Step step;
                if (step_node.contains("transpile_exec"))
                    step.transpile_exec = step_node["transpile_exec"].get_value<std::string>();
                if (step_node.contains("transpile_query"))
                    step.transpile_query = step_node["transpile_query"].get_value<std::string>();
                tc.steps.push_back(std::move(step));
            }
        }

        tests.push_back(std::move(tc));
    }
    return tests;
}

} // namespace

// ── Test runner ──────────────────────────────────────────────────

TEST_CASE("sqlite integration") {
    auto tests = load_tests();
    for (const auto& tc : tests) {
        SUBCASE(tc.name.c_str()) {
            Db g;

            // Setup
            for (const auto& sql : tc.setup)
                exec(g.db, sql);

            std::vector<std::string> rows;

            if (!tc.steps.empty()) {
                // Multi-step test
                for (const auto& step : tc.steps) {
                    if (!step.transpile_exec.empty()) {
                        auto sql = transpile_str(step.transpile_exec);
                        exec(g.db, sql);
                    }
                    if (!step.transpile_query.empty()) {
                        auto sql = transpile_str(step.transpile_query);
                        rows = query(g.db, sql);
                    }
                }
            } else if (!tc.raw_sql.empty()) {
                // Direct SQL (no transpile)
                rows = query(g.db, tc.raw_sql);
            } else if (!tc.fks.empty()) {
                // FK-guided transpile
                std::vector<std::vector<sqldeep_column_pair>> col_storage;
                std::vector<sqldeep_foreign_key> cfks;
                for (const auto& fk : tc.fks) {
                    col_storage.emplace_back();
                    for (const auto& [from, to] : fk.columns)
                        col_storage.back().push_back({from.c_str(), to.c_str()});
                    cfks.push_back({
                        fk.from_table.c_str(), fk.to_table.c_str(),
                        col_storage.back().data(),
                        static_cast<int>(col_storage.back().size())
                    });
                }
                auto sql = transpile_fk_str(tc.input, cfks.data(),
                                             static_cast<int>(cfks.size()));
                rows = query(g.db, sql);
            } else {
                // Normal transpile + query
                auto sql = transpile_str(tc.input);
                rows = query(g.db, sql);
            }

            // Verify
            if (!tc.expected_contains.empty()) {
                REQUIRE(rows.size() == 1);
                for (const auto& needle : tc.expected_contains)
                    CHECK_MESSAGE(rows[0].find(needle) != std::string::npos,
                                  "missing: " << needle << " in " << rows[0]);
            } else {
                REQUIRE(rows.size() == tc.expected.size());
                for (size_t i = 0; i < tc.expected.size(); ++i)
                    CHECK(rows[i] == tc.expected[i]);
            }
        }
    }
}

// ── Bind-parameter integration ──────────────────────────────────────
//
// The YAML cases prove the transpiled text is correct; these prove the
// restored "?" placeholders actually bind by position end-to-end, and
// that an order-destroying rewrite is rejected before it can misbind.

namespace {

// Run a single-column query with positional text bindings applied left
// to right, returning the lone result cell.
std::string query_bound(sqlite3* db, const std::string& sql,
                         const std::vector<std::string>& binds) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(
            std::string("prepare: ") + sqlite3_errmsg(db) + "\nSQL: " + sql);
    for (size_t i = 0; i < binds.size(); ++i)
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), binds[i].c_str(),
                          -1, SQLITE_TRANSIENT);
    std::string out = "NULL";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) out = text;
    }
    sqlite3_finalize(stmt);
    return out;
}

} // namespace

TEST_CASE("positional parameters bind by source order") {
    Db g;
    exec(g.db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES (1, 'alice'), (2, 'bob')");

    SUBCASE("placeholder in WHERE binds the filter") {
        auto sql = transpile_str("SELECT { id, name } FROM t WHERE id = ?");
        CHECK(query_bound(g.db, sql, {"2"}) == R"({"id":2,"name":"bob"})");
    }

    SUBCASE("projection then clause keeps left-to-right binding") {
        // The first "?" is the projection value, the second is the filter.
        // If the rewrite swapped them, lo/hi and the row would be wrong.
        auto sql = transpile_str("SELECT { lo: ?, hi: ? } FROM t WHERE id = ?");
        CHECK(query_bound(g.db, sql, {"A", "B", "1"})
              == R"({"lo":"A","hi":"B"})");
    }

    SUBCASE("from-first numbered parameters bind by index after reorder") {
        // ?1 is the filter (authored first), ?2 the projection (emitted
        // first). Numbered params bind by index regardless of position.
        auto sql = transpile_str("FROM t WHERE id = ?1 SELECT { tag: ?2 }");
        CHECK(query_bound(g.db, sql, {"2", "x"}) == R"({"tag":"x"})");
    }

    SUBCASE("order-destroying rewrite is rejected, not silently wrong") {
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile("FROM t WHERE id = ? SELECT { tag: ? }",
                                      &err, &line, &col);
        CHECK(out == nullptr);
        REQUIRE(err != nullptr);
        CHECK(std::string(err).find("positional parameter")
              != std::string::npos);
        sqldeep_free(err);
    }
}
