// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>
#include "sqldeep.h"

using sqldeep::transpile;
using sqldeep::Error;

// ── Basic object ────────────────────────────────────────────────────

TEST_CASE("basic object select") {
    CHECK(transpile("SELECT { id, name } FROM t") ==
          "SELECT json_object('id', id, 'name', name) FROM t");
}

TEST_CASE("single bare field") {
    CHECK(transpile("SELECT { id } FROM t") ==
          "SELECT json_object('id', id) FROM t");
}

// ── Renamed / quoted fields ─────────────────────────────────────────

TEST_CASE("renamed field") {
    CHECK(transpile("SELECT { order_id: id } FROM t") ==
          "SELECT json_object('order_id', id) FROM t");
}

TEST_CASE("double-quoted key") {
    CHECK(transpile("SELECT { \"order id\": id } FROM t") ==
          "SELECT json_object('order id', id) FROM t");
}

TEST_CASE("mixed bare and renamed fields") {
    CHECK(transpile("SELECT { id, order_id: oid, name } FROM t") ==
          "SELECT json_object('id', id, 'order_id', oid, 'name', name) FROM t");
}

// ── Trailing commas ─────────────────────────────────────────────────

TEST_CASE("trailing comma in object") {
    CHECK(transpile("SELECT { id, name, } FROM t") ==
          "SELECT json_object('id', id, 'name', name) FROM t");
}

TEST_CASE("trailing comma in array") {
    CHECK(transpile("SELECT { tags: [1, 2, 3, ] } FROM t") ==
          "SELECT json_object('tags', json_array(1, 2, 3)) FROM t");
}

// ── Inline arrays ───────────────────────────────────────────────────

TEST_CASE("inline array") {
    CHECK(transpile("SELECT { tags: [1, 2, 3] } FROM t") ==
          "SELECT json_object('tags', json_array(1, 2, 3)) FROM t");
}

TEST_CASE("inline array with expressions") {
    CHECK(transpile("SELECT { vals: [a + 1, b * 2] } FROM t") ==
          "SELECT json_object('vals', json_array(a + 1, b * 2)) FROM t");
}

// ── Expression with commas ──────────────────────────────────────────

TEST_CASE("expression with commas in parens") {
    CHECK(transpile("SELECT { result: coalesce(a, b) } FROM t") ==
          "SELECT json_object('result', coalesce(a, b)) FROM t");
}

TEST_CASE("nested function calls") {
    CHECK(transpile("SELECT { v: max(min(a, b), c) } FROM t") ==
          "SELECT json_object('v', max(min(a, b), c)) FROM t");
}

// ── String literals with special chars ──────────────────────────────

TEST_CASE("string literal with comma") {
    CHECK(transpile("SELECT { label: 'a, b' } FROM t") ==
          "SELECT json_object('label', 'a, b') FROM t");
}

TEST_CASE("string literal with brace") {
    CHECK(transpile("SELECT { label: 'hello }' } FROM t") ==
          "SELECT json_object('label', 'hello }') FROM t");
}

// ── Comments ────────────────────────────────────────────────────────

TEST_CASE("line comment stripped") {
    CHECK(transpile("// comment\nSELECT { id } FROM t") ==
          "SELECT json_object('id', id) FROM t");
}

TEST_CASE("inline comment stripped") {
    CHECK(transpile("SELECT { id, // field\nname } FROM t") ==
          "SELECT json_object('id', id, 'name', name) FROM t");
}

// ── SQL passthrough ─────────────────────────────────────────────────

TEST_CASE("plain SQL passthrough") {
    CHECK(transpile("SELECT id, name FROM t WHERE x > 0") ==
          "SELECT id, name FROM t WHERE x > 0");
}

TEST_CASE("empty input") {
    CHECK(transpile("") == "");
}

TEST_CASE("only comments") {
    CHECK(transpile("// just a comment") == "");
}

// ── Two-level nesting: object subquery ──────────────────────────────

TEST_CASE("two-level object nesting") {
    CHECK(transpile(
        "SELECT { id, address: SELECT { street, city } FROM addresses WHERE uid = u.id } FROM users u") ==
        "SELECT json_object('id', id, 'address', "
        "(SELECT json_group_array(json_object('street', street, 'city', city)) FROM addresses WHERE uid = u.id)"
        ") FROM users u");
}

// ── Two-level nesting: array subquery ───────────────────────────────

TEST_CASE("two-level nesting with array subquery") {
    CHECK(transpile(
        "SELECT { id, totals: SELECT [total] FROM orders WHERE cid = c.id } FROM customers c") ==
        "SELECT json_object('id', id, 'totals', "
        "(SELECT json_group_array(total) FROM orders WHERE cid = c.id)"
        ") FROM customers c");
}

// ── Three-level nesting ─────────────────────────────────────────────

TEST_CASE("three-level nesting") {
    auto input = R"(SELECT {
    id, name,
    orders: SELECT {
        order_id: id,
        items: SELECT { item: 'item-' || name, qty } FROM items i WHERE order_id = o.id,
    } FROM orders o WHERE customer_id = p.id,
} FROM people p)";

    auto expected =
        "SELECT json_object('id', id, 'name', name, 'orders', "
        "(SELECT json_group_array(json_object('order_id', id, 'items', "
        "(SELECT json_group_array(json_object('item', 'item-' || name, 'qty', qty)) "
        "FROM items i WHERE order_id = o.id)"
        ")) FROM orders o WHERE customer_id = p.id)"
        ") FROM people p";

    CHECK(transpile(input) == expected);
}

// ── Mixed array and object nesting ──────────────────────────────────

TEST_CASE("mixed array and object nesting") {
    auto input = R"(SELECT {
    id,
    tags: [1, 2, 3],
    orders: SELECT {
        order_id: id,
        line_items: SELECT [name] FROM items WHERE oid = o.id,
    } FROM orders o WHERE cid = c.id,
} FROM customers c)";

    auto expected =
        "SELECT json_object('id', id, "
        "'tags', json_array(1, 2, 3), "
        "'orders', (SELECT json_group_array(json_object("
        "'order_id', id, "
        "'line_items', (SELECT json_group_array(name) FROM items WHERE oid = o.id)"
        ")) FROM orders o WHERE cid = c.id)"
        ") FROM customers c";

    CHECK(transpile(input) == expected);
}

// ── Array subquery with expression ──────────────────────────────────

TEST_CASE("array subquery with expression") {
    CHECK(transpile(
        "SELECT { ids: SELECT [id * 10] FROM t } FROM u") ==
        "SELECT json_object('ids', (SELECT json_group_array(id * 10) FROM t)) FROM u");
}

// ── Deep construct in tail ──────────────────────────────────────────

TEST_CASE("deep construct in WHERE subquery") {
    CHECK(transpile(
        "SELECT { id } FROM t WHERE x IN (SELECT { y } FROM t2)") ==
        "SELECT json_object('id', id) FROM t WHERE x IN (SELECT json_group_array(json_object('y', y)) FROM t2)");
}

// ── Multiple deep selects ───────────────────────────────────────────

TEST_CASE("multiple fields with subqueries") {
    CHECK(transpile(
        "SELECT { a: SELECT { x } FROM t1, b: SELECT { y } FROM t2 } FROM t") ==
        "SELECT json_object("
        "'a', (SELECT json_group_array(json_object('x', x)) FROM t1), "
        "'b', (SELECT json_group_array(json_object('y', y)) FROM t2)"
        ") FROM t");
}

// ── Error cases ─────────────────────────────────────────────────────

TEST_CASE("unterminated brace") {
    CHECK_THROWS_AS(transpile("SELECT { id, name"), Error);
}

TEST_CASE("unterminated bracket") {
    CHECK_THROWS_AS(transpile("SELECT { tags: [1, 2"), Error);
}

TEST_CASE("error has position info") {
    try {
        transpile("SELECT { 123 }");
        FAIL("expected Error");
    } catch (const Error& e) {
        CHECK(e.line() == 1);
        CHECK(e.col() > 0);
    }
}

TEST_CASE("missing value after colon") {
    CHECK_THROWS_AS(transpile("SELECT { key: } FROM t"), Error);
}
