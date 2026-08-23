// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// C API contract tests: NULL input and exception containment.

#include <doctest.h>
#include "sqldeep.h"

#include <cstdio>
#include <cstring>
#include <string>

TEST_CASE("C API rejects NULL input without crashing") {
    SUBCASE("sqldeep_transpile(NULL)") {
        char* err = nullptr;
        int line = -1, col = -1;
        char* out = sqldeep_transpile(nullptr, &err, &line, &col);
        CHECK(out == nullptr);
        REQUIRE(err != nullptr);
        CHECK(std::string(err).find("NULL input") != std::string::npos);
        CHECK(line == 0);
        CHECK(col == 0);
        sqldeep_free(err);
    }

    SUBCASE("sqldeep_transpile_fk_backend(NULL)") {
        char* err = nullptr;
        int line = -1, col = -1;
        char* out = sqldeep_transpile_fk_backend(
            nullptr, SQLDEEP_SQLITE, nullptr, 0, &err, &line, &col);
        CHECK(out == nullptr);
        REQUIRE(err != nullptr);
        CHECK(std::string(err).find("NULL input") != std::string::npos);
        sqldeep_free(err);
    }

    SUBCASE("sqldeep_transpile_backend(NULL, postgres)") {
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile_backend(
            nullptr, SQLDEEP_POSTGRES, &err, &line, &col);
        CHECK(out == nullptr);
        REQUIRE(err != nullptr);
        CHECK(std::string(err).find("NULL input") != std::string::npos);
        sqldeep_free(err);
    }
}

TEST_CASE("C API returns NULL + err_msg on parse error") {
    char* err = nullptr;
    int line = 0, col = 0;
    char* out = sqldeep_transpile("SELECT {", &err, &line, &col);
    CHECK(out == nullptr);
    REQUIRE(err != nullptr);
    CHECK(std::strlen(err) > 0);
    sqldeep_free(err);
}

TEST_CASE("bare-expression fail-closed (T7.3)") {
    const char* cases[] = {"a, b", "1, 2", "1 UNION SELECT 2", "x FROM t"};
    for (const char* input : cases) {
        CAPTURE(input);
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile(input, &err, &line, &col);
        CHECK(out == nullptr);
        REQUIRE(err != nullptr);
        CHECK(std::strlen(err) > 0);
        sqldeep_free(err);
    }

    // Valid bare expressions still succeed with non-empty SQL.
    {
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile("{a, b}", &err, &line, &col);
        REQUIRE(out != nullptr);
        CHECK(std::strlen(out) > 0);
        CHECK(std::string(out).find("sqldeep_json_object") != std::string::npos);
        sqldeep_free(out);
        if (err) sqldeep_free(err);
    }
    {
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile("<div/>", &err, &line, &col);
        REQUIRE(out != nullptr);
        CHECK(std::strlen(out) > 0);
        sqldeep_free(out);
        if (err) sqldeep_free(err);
    }
}

TEST_CASE("CLI surfaces bare-expression syntax errors (T7.3)") {
    // Depends on an up-to-date build/sqldeep (cv shell). Skip if absent.
    FILE* probe = std::fopen("build/sqldeep", "rb");
    if (!probe) {
        WARN("build/sqldeep missing — run `cv shell` to enable CLI check");
        return;
    }
    std::fclose(probe);

    FILE* pipe = popen(
        "(printf 'a, b;\\n' | ./build/sqldeep :memory: 2>&1); echo EXIT:$?",
        "r");
    REQUIRE(pipe != nullptr);
    std::string output;
    char buf[256];
    while (std::fgets(buf, sizeof buf, pipe)) output += buf;
    pclose(pipe);

    CHECK_MESSAGE(output.find("EXIT:0") == std::string::npos,
                  "CLI should exit non-zero; output: " << output);
    bool mentions_error =
        output.find("error") != std::string::npos
        || output.find("Error") != std::string::npos
        || output.find("Parse") != std::string::npos
        || output.find("near") != std::string::npos;
    CHECK_MESSAGE(mentions_error, "CLI output: " << output);
}

TEST_CASE("CLI surfaces transform errors instead of SQLite parse noise (T6)") {
    // Depends on an up-to-date build/sqldeep (cv shell). Skip if absent.
    FILE* probe = std::fopen("build/sqldeep", "rb");
    if (!probe) {
        WARN("build/sqldeep missing — run `cv shell` to enable CLI check");
        return;
    }
    std::fclose(probe);

    auto run_cli = [](const char* sql) -> std::string {
        // Escape single quotes for the shell printf argument.
        std::string escaped;
        for (const char* p = sql; *p; ++p) {
            if (*p == '\'') escaped += "'\\''";
            else escaped += *p;
        }
        std::string cmd =
            "(printf '" + escaped + "\\n' | ./build/sqldeep :memory: 2>&1);"
            " echo EXIT:$?";
        FILE* pipe = popen(cmd.c_str(), "r");
        REQUIRE(pipe != nullptr);
        std::string output;
        char buf[256];
        while (std::fgets(buf, sizeof buf, pipe)) output += buf;
        pclose(pipe);
        return output;
    };

    SUBCASE("positional-param reorder surfaces sqldeep message") {
        std::string output =
            run_cli("FROM t WHERE a = ? SELECT { b: ? };");
        CHECK_MESSAGE(output.find("EXIT:0") == std::string::npos,
                      "CLI should exit non-zero; output: " << output);
        bool has_transform =
            output.find("positional") != std::string::npos
            || output.find("reordered") != std::string::npos;
        CHECK_MESSAGE(has_transform,
                      "expected transform error; output: " << output);
        // Must not be the misleading raw-SQL parse fallthrough.
        CHECK_MESSAGE(output.find("unrecognized token") == std::string::npos,
                      "got SQLite token error; output: " << output);
        CHECK_MESSAGE(output.find("near \"FROM\"") == std::string::npos,
                      "got SQLite FROM syntax error; output: " << output);
    }

    SUBCASE("unknown join alias surfaces sqldeep message") {
        std::string output =
            run_cli("SELECT { orders_id } FROM x->orders;");
        CHECK_MESSAGE(output.find("EXIT:0") == std::string::npos,
                      "CLI should exit non-zero; output: " << output);
        CHECK_MESSAGE(output.find("unknown join alias") != std::string::npos,
                      "expected transform error; output: " << output);
        CHECK_MESSAGE(output.find("unrecognized token") == std::string::npos,
                      "got SQLite token error; output: " << output);
    }

    SUBCASE("EXPLAIN still falls through unchanged") {
        std::string output = run_cli("EXPLAIN SELECT 1;");
        CHECK_MESSAGE(output.find("EXIT:0") != std::string::npos,
                      "EXPLAIN should succeed; output: " << output);
        bool has_explain =
            output.find("Init") != std::string::npos
            || output.find("opcode") != std::string::npos;
        CHECK_MESSAGE(has_explain,
                      "expected EXPLAIN bytecode; output: " << output);
    }

    SUBCASE("plain SQL still falls through unchanged") {
        std::string output = run_cli("SELECT 1;");
        CHECK_MESSAGE(output.find("EXIT:0") != std::string::npos,
                      "SELECT 1 should succeed; output: " << output);
        bool has_row =
            output.find("\n1\n") != std::string::npos
            || output.find("1\nEXIT") != std::string::npos;
        CHECK_MESSAGE(has_row,
                      "expected result row; output: " << output);
    }
}

TEST_CASE("RECURSE quotes identifiers and rejects unsafe PK domains (T7.2/T7.4)") {
    SUBCASE("quoted table with space round-trips") {
        char* err = nullptr; int line = 0, col = 0;
        char* out = sqldeep_transpile(
            "SELECT/1 { id, name, children: * } FROM \"order items\" "
            "RECURSE ON (parent_id) WHERE parent_id IS NULL",
            &err, &line, &col);
        REQUIRE(out != nullptr);
        CHECK(std::string(out).find("FROM \"order items\"") != std::string::npos);
        // Must not appear unquoted as two tokens.
        CHECK(std::string(out).find("FROM order items") == std::string::npos);
        sqldeep_free(out);
    }

    SUBCASE("structural injection stays inside a quoted table name") {
        char* err = nullptr; int line = 0, col = 0;
        const char* evil =
            "SELECT/1 { id, name, children: * } FROM "
            "\"t WHERE 1=1) SELECT secret FROM users;--\" RECURSE ON (parent_id)";
        char* out = sqldeep_transpile(evil, &err, &line, &col);
        REQUIRE(out != nullptr);
        std::string sql(out);
        // Payload only appears as a quoted identifier.
        CHECK(sql.find("FROM \"t WHERE 1=1) SELECT secret FROM users;--\"")
              != std::string::npos);
        // Re-parsed shape is still a single WITH RECURSIVE statement.
        CHECK(sql.rfind("WITH RECURSIVE", 0) == 0);
        CHECK(sql.find("; SELECT secret") == std::string::npos);
        // Every mention of the payload is only as part of the quoted
        // table identifier (appears in both anchor and recursive FROM).
        size_t pos = 0;
        int hits = 0;
        while ((pos = sql.find("SELECT secret FROM users", pos)) != std::string::npos) {
            // Must be inside "...SELECT secret FROM users;--".
            CHECK(pos >= 1);
            CHECK(sql.compare(pos, 24, "SELECT secret FROM users") == 0);
            // Walk back to nearest quote — should be the opening quote of
            // the forged table name.
            auto q = sql.rfind('"', pos);
            REQUIRE(q != std::string::npos);
            CHECK(sql.compare(q, 10, "\"t WHERE 1") == 0);
            ++hits;
            pos += 24;
        }
        CHECK(hits >= 1);
        sqldeep_free(out);
    }

    SUBCASE("path uses rank pad not %010d of raw pk") {
        char* err = nullptr; int line = 0, col = 0;
        char* out = sqldeep_transpile(
            "SELECT/1 { id, name, children: * } FROM categories "
            "RECURSE ON (parent_id) WHERE parent_id IS NULL",
            &err, &line, &col);
        REQUIRE(out != nullptr);
        std::string sql(out);
        CHECK(sql.find("printf('%010d'") == std::string::npos);
        CHECK(sql.find("printf('%019d'") != std::string::npos);
        CHECK(sql.find("ROW_NUMBER()") != std::string::npos);
        sqldeep_free(out);
    }
}
