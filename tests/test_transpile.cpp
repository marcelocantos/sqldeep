// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>
#include <fkYAML/node.hpp>
#include <fstream>
#include <string>
#include <vector>

#include "sqldeep.h"

// ── Test helpers ─────────────────────────────────────────────────────

namespace {

// RAII wrapper for malloc'd strings returned by the C API.
struct SdStr {
    char* p;
    explicit SdStr(char* s) : p(s) {}
    ~SdStr() { if (p) sqldeep_free(p); }
    SdStr(const SdStr&) = delete;
    SdStr& operator=(const SdStr&) = delete;
};

// Test-local error type matching the old sqldeep::Error interface.
struct Error {
    std::string msg;
    int line_, col_;
    int line() const { return line_; }
    int col() const { return col_; }
};

// Owned FK data parsed from YAML.
struct OwnedFk {
    std::string from_table, to_table;
    struct Col { std::string from_column, to_column; };
    std::vector<Col> columns;
};

// ── transpile wrappers (C API → C++ exceptions) ─────────────────────

std::string transpile(const std::string& input) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    SdStr result{sqldeep_transpile(input.c_str(), &err_msg, &err_line, &err_col)};
    if (!result.p) {
        SdStr err{err_msg};
        throw Error{err.p ? err.p : "", err_line, err_col};
    }
    return result.p;
}

std::string transpile(const std::string& input, sqldeep_backend backend) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    SdStr result{sqldeep_transpile_backend(input.c_str(), backend,
                                            &err_msg, &err_line, &err_col)};
    if (!result.p) {
        SdStr err{err_msg};
        throw Error{err.p ? err.p : "", err_line, err_col};
    }
    return result.p;
}

// Build C FK structs from owned data and call the FK transpile API.
std::string transpile(const std::string& input,
                       const std::vector<OwnedFk>& fks,
                       sqldeep_backend backend = SQLDEEP_SQLITE) {
    // Build column pairs
    std::vector<std::vector<sqldeep_column_pair>> all_cols;
    all_cols.reserve(fks.size());
    for (const auto& f : fks) {
        auto& cols = all_cols.emplace_back();
        for (const auto& c : f.columns)
            cols.push_back({c.from_column.c_str(), c.to_column.c_str()});
    }
    // Build FK structs
    std::vector<sqldeep_foreign_key> c_fks;
    c_fks.reserve(fks.size());
    for (size_t i = 0; i < fks.size(); ++i) {
        c_fks.push_back({fks[i].from_table.c_str(), fks[i].to_table.c_str(),
                          all_cols[i].data(),
                          static_cast<int>(all_cols[i].size())});
    }
    // Call C API
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    SdStr result{sqldeep_transpile_fk_backend(
        input.c_str(), backend,
        c_fks.data(), static_cast<int>(c_fks.size()),
        &err_msg, &err_line, &err_col)};
    if (!result.p) {
        SdStr err{err_msg};
        throw Error{err.p ? err.p : "", err_line, err_col};
    }
    return result.p;
}

// ── YAML loader ─────────────────────────────────────────────────────

const fkyaml::node& test_data() {
    static const auto data = [] {
        std::ifstream ifs("testdata/transpile.yaml");
        REQUIRE(ifs.good());
        return fkyaml::node::deserialize(ifs);
    }();
    return data;
}

// Convert YAML fks array to owned FK data.
std::vector<OwnedFk> to_fks(const fkyaml::node& node) {
    std::vector<OwnedFk> fks;
    for (const auto& fk : node) {
        OwnedFk f;
        f.from_table = fk["from_table"].get_value<std::string>();
        f.to_table = fk["to_table"].get_value<std::string>();
        for (const auto& col : fk["columns"]) {
            f.columns.push_back({
                col["from_column"].get_value<std::string>(),
                col["to_column"].get_value<std::string>(),
            });
        }
        fks.push_back(std::move(f));
    }
    return fks;
}

} // namespace

// ── Convention transpile tests ──────────────────────────────────────

TEST_CASE("convention transpile") {
    for (const auto& tc : test_data()["convention"]) {
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            auto expected = tc["expected"].get_value<std::string>();
            CHECK(transpile(input) == expected);
        }
    }
}

// ── FK transpile tests ──────────────────────────────────────────────

TEST_CASE("fk transpile") {
    for (const auto& tc : test_data()["fk"]) {
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            auto expected = tc["expected"].get_value<std::string>();
            auto fks = to_fks(tc["fks"]);
            CHECK(transpile(input, fks) == expected);
        }
    }
}

// ── PostgreSQL convention transpile tests ────────────────────────────

TEST_CASE("convention transpile (postgres)") {
    for (const auto& tc : test_data()["convention"]) {
        if (!tc.contains("expected_postgres")) continue;
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            auto expected = tc["expected_postgres"].get_value<std::string>();
            CHECK(transpile(input, SQLDEEP_POSTGRES) == expected);
        }
    }
}

// ── PostgreSQL FK transpile tests ───────────────────────────────────

TEST_CASE("fk transpile (postgres)") {
    for (const auto& tc : test_data()["fk"]) {
        if (!tc.contains("expected_postgres")) continue;
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            auto expected = tc["expected_postgres"].get_value<std::string>();
            auto fks = to_fks(tc["fks"]);
            CHECK(transpile(input, fks, SQLDEEP_POSTGRES) == expected);
        }
    }
}

// ── Convention error tests ──────────────────────────────────────────

TEST_CASE("convention errors") {
    for (const auto& tc : test_data()["errors"]) {
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            if (tc.contains("error_line")) {
                try {
                    transpile(input);
                    FAIL("expected Error");
                } catch (const Error& e) {
                    CHECK(e.line() == tc["error_line"].get_value<int>());
                    if (tc.contains("error_col_gt")) {
                        CHECK(e.col() > tc["error_col_gt"].get_value<int>());
                    }
                }
            } else {
                CHECK_THROWS_AS(transpile(input), Error);
            }
        }
    }
}

// ── FK error tests ──────────────────────────────────────────────────

TEST_CASE("fk errors") {
    for (const auto& tc : test_data()["fk_errors"]) {
        auto name = tc["name"].get_value<std::string>();
        SUBCASE(name.c_str()) {
            auto input = tc["input"].get_value<std::string>();
            auto fks = to_fks(tc["fks"]);
            CHECK_THROWS_AS(transpile(input, fks), Error);
        }
    }
}

// ── Programmatic tests (not in YAML) ────────────────────────────────

TEST_CASE("excessive nesting depth throws") {
    // Build deeply nested input: SELECT { a: SELECT { a: ... } FROM t } FROM t
    std::string input;
    for (int i = 0; i < 250; ++i)
        input += "SELECT { a: ";
    input += "1";
    for (int i = 0; i < 250; ++i)
        input += " } FROM t";
    CHECK_THROWS_AS(transpile(input), Error);
}
