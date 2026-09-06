// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// C API contract tests: every extern "C" entry point must honour the
// header contract (NULL return + err_msg on failure, never a crash) for
// hostile or degenerate arguments that FFI callers can pass.

#include <doctest.h>
#include <string>

#include "sqldeep.h"

namespace {

// Owns the two malloc'd strings a transpile call can hand back.
struct Call {
    char* out = nullptr;
    char* err = nullptr;
    int line = -1, col = -1;
    ~Call() { sqldeep_free(out); sqldeep_free(err); }
    std::string err_str() const { return err ? err : ""; }
};

const sqldeep_column_pair kPair{"cid", "id"};
const sqldeep_foreign_key kFk{"orders", "customers", &kPair, 1};

} // namespace

TEST_CASE("NULL input is an error, not a crash") {
    SUBCASE("sqldeep_transpile") {
        Call c;
        c.out = sqldeep_transpile(nullptr, &c.err, &c.line, &c.col);
        CHECK(c.out == nullptr);
        REQUIRE(c.err != nullptr);
        CHECK(c.err_str().find("NULL") != std::string::npos);
        CHECK(c.line == 0);
        CHECK(c.col == 0);
    }
    SUBCASE("sqldeep_transpile_fk") {
        Call c;
        c.out = sqldeep_transpile_fk(nullptr, &kFk, 1, &c.err, &c.line, &c.col);
        CHECK(c.out == nullptr);
        REQUIRE(c.err != nullptr);
        CHECK(c.err_str().find("NULL") != std::string::npos);
    }
    SUBCASE("sqldeep_transpile_backend") {
        Call c;
        c.out = sqldeep_transpile_backend(nullptr, SQLDEEP_POSTGRES,
                                          &c.err, &c.line, &c.col);
        CHECK(c.out == nullptr);
        REQUIRE(c.err != nullptr);
    }
    SUBCASE("sqldeep_transpile_fk_backend") {
        Call c;
        c.out = sqldeep_transpile_fk_backend(nullptr, SQLDEEP_SQLITE, &kFk, 1,
                                             &c.err, &c.line, &c.col);
        CHECK(c.out == nullptr);
        REQUIRE(c.err != nullptr);
    }
    SUBCASE("error out-params may all be NULL") {
        char* out = sqldeep_transpile(nullptr, nullptr, nullptr, nullptr);
        CHECK(out == nullptr);
    }
}

TEST_CASE("NULL foreign-key array with a positive count is an error") {
    Call c;
    c.out = sqldeep_transpile_fk("SELECT { id } FROM t", nullptr, 3,
                                 &c.err, &c.line, &c.col);
    CHECK(c.out == nullptr);
    REQUIRE(c.err != nullptr);
    CHECK(c.err_str().find("NULL") != std::string::npos);
}

TEST_CASE("NULL foreign-key array with zero count is convention mode") {
    Call c;
    c.out = sqldeep_transpile_fk("SELECT { id } FROM t", nullptr, 0,
                                 &c.err, &c.line, &c.col);
    REQUIRE(c.out != nullptr);
    CHECK(std::string(c.out) == "SELECT sqldeep_json_object('id', id) FROM t");
    CHECK(c.err == nullptr);
}
