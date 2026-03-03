// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>
#include <fkYAML/node.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sqldeep.h"

using sqldeep::transpile;
using sqldeep::Backend;
using sqldeep::Error;
using sqldeep::ForeignKey;

// ── YAML loader (shared across all TEST_CASEs) ─────────────────────

static const fkyaml::node& test_data() {
    static const auto data = [] {
        std::ifstream ifs("testdata/transpile.yaml");
        REQUIRE(ifs.good());
        return fkyaml::node::deserialize(ifs);
    }();
    return data;
}

// Convert YAML fks array to std::vector<ForeignKey>.
static std::vector<ForeignKey> to_fks(const fkyaml::node& node) {
    std::vector<ForeignKey> fks;
    for (const auto& fk : node) {
        ForeignKey f;
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
            CHECK(transpile(input, Backend::postgres) == expected);
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
            CHECK(transpile(input, fks, Backend::postgres) == expected);
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
