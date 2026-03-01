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

// ── Key escaping ───────────────────────────────────────────────────

TEST_CASE("single-quote in double-quoted key is escaped") {
    CHECK(transpile("SELECT { \"it's\": v } FROM t") ==
          "SELECT json_object('it''s', v) FROM t");
}

TEST_CASE("backslash-escaped quote in double-quoted key") {
    CHECK(transpile("SELECT { \"say \\\"hello\\\"\": v } FROM t") ==
          "SELECT json_object('say \"hello\"', v) FROM t");
}

// ── SQL doubled-quote strings ──────────────────────────────────────

TEST_CASE("SQL doubled single-quote passthrough") {
    CHECK(transpile("SELECT { name: 'O''Brien' } FROM t") ==
          "SELECT json_object('name', 'O''Brien') FROM t");
}

TEST_CASE("SQL doubled double-quote in key") {
    CHECK(transpile("SELECT { \"say \"\"hi\"\"\": v } FROM t") ==
          "SELECT json_object('say \"hi\"', v) FROM t");
}

// ── Nesting depth ──────────────────────────────────────────────────

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

// ── Unmatched paren ────────────────────────────────────────────────

TEST_CASE("unmatched closing paren throws") {
    CHECK_THROWS_AS(transpile("SELECT { id } FROM t )"), Error);
}

// ── Auto-join (-> syntax) ─────────────────────────────────────────

TEST_CASE("basic auto-join") {
    CHECK(transpile(
        "SELECT { customers_id, name, "
        "orders: SELECT { orders_id } FROM c->orders "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, 'name', name, "
        "'orders', (SELECT json_group_array(json_object('orders_id', orders_id)) "
        "FROM orders WHERE orders.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("auto-join with child alias") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "orders: SELECT { orders_id } FROM c->orders o ORDER BY orders_id "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'orders', (SELECT json_group_array(json_object('orders_id', orders_id)) "
        "FROM orders o WHERE o.customers_id = c.customers_id ORDER BY orders_id)"
        ") FROM customers c");
}

TEST_CASE("three-level auto-join") {
    auto input = R"(SELECT {
    customers_id, name,
    orders: SELECT {
        orders_id,
        items: SELECT { items_id, qty } FROM o->items i ORDER BY items_id,
    } FROM c->orders o ORDER BY orders_id,
} FROM customers c)";

    auto expected =
        "SELECT json_object('customers_id', customers_id, 'name', name, 'orders', "
        "(SELECT json_group_array(json_object('orders_id', orders_id, 'items', "
        "(SELECT json_group_array(json_object('items_id', items_id, 'qty', qty)) "
        "FROM items i WHERE i.orders_id = o.orders_id ORDER BY items_id)"
        ")) FROM orders o WHERE o.customers_id = c.customers_id ORDER BY orders_id)"
        ") FROM customers c";

    CHECK(transpile(input) == expected);
}

TEST_CASE("auto-join unknown alias throws") {
    CHECK_THROWS_AS(transpile(
        "SELECT { orders_id } FROM x->orders"), Error);
}

TEST_CASE("auto-join coexists with explicit WHERE") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "orders: SELECT { orders_id } FROM c->orders, "
        "notes: SELECT { note } FROM notes WHERE notes.customers_id = c.customers_id "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'orders', (SELECT json_group_array(json_object('orders_id', orders_id)) "
        "FROM orders WHERE orders.customers_id = c.customers_id), "
        "'notes', (SELECT json_group_array(json_object('note', note)) "
        "FROM notes WHERE notes.customers_id = c.customers_id)"
        ") FROM customers c");
}

// ── FROM-first syntax ──────────────────────────────────────────────

TEST_CASE("from-first basic") {
    CHECK(transpile("FROM t SELECT { id, name }") ==
          "SELECT json_object('id', id, 'name', name) FROM t");
}

TEST_CASE("from-first with WHERE") {
    CHECK(transpile("FROM t WHERE x > 0 SELECT { id }") ==
          "SELECT json_object('id', id) FROM t WHERE x > 0");
}

TEST_CASE("from-first with ORDER BY") {
    CHECK(transpile("FROM t ORDER BY id SELECT { id, name }") ==
          "SELECT json_object('id', id, 'name', name) FROM t ORDER BY id");
}

TEST_CASE("from-first with trailing comma") {
    CHECK(transpile("FROM t SELECT { id, name, }") ==
          "SELECT json_object('id', id, 'name', name) FROM t");
}

TEST_CASE("from-first with comment") {
    CHECK(transpile("// query\nFROM t SELECT { id }") ==
          "SELECT json_object('id', id) FROM t");
}

TEST_CASE("from-first array projection") {
    CHECK(transpile("FROM orders WHERE cid = 1 SELECT [total]") ==
          "SELECT json_group_array(total) FROM orders WHERE cid = 1");
}

TEST_CASE("from-first nested in field value") {
    CHECK(transpile(
        "FROM customers c SELECT { id, name, "
        "orders: FROM orders o WHERE o.cid = c.id SELECT { total } }") ==
        "SELECT json_object('id', id, 'name', name, 'orders', "
        "(SELECT json_group_array(json_object('total', total)) "
        "FROM orders o WHERE o.cid = c.id)"
        ") FROM customers c");
}

TEST_CASE("from-first with auto-join") {
    CHECK(transpile(
        "FROM customers c SELECT { customers_id, name, "
        "orders: FROM c->orders SELECT { orders_id } }") ==
        "SELECT json_object('customers_id', customers_id, 'name', name, "
        "'orders', (SELECT json_group_array(json_object('orders_id', orders_id)) "
        "FROM orders WHERE orders.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("from-first three-level nesting") {
    auto input = R"(FROM customers c SELECT {
    customers_id, name,
    orders: FROM c->orders o ORDER BY orders_id SELECT {
        orders_id,
        items: FROM o->items ORDER BY items_id SELECT { name, qty },
    },
})";

    auto expected =
        "SELECT json_object('customers_id', customers_id, 'name', name, 'orders', "
        "(SELECT json_group_array(json_object('orders_id', orders_id, 'items', "
        "(SELECT json_group_array(json_object('name', name, 'qty', qty)) "
        "FROM items WHERE items.orders_id = o.orders_id ORDER BY items_id)"
        ")) FROM orders o WHERE o.customers_id = c.customers_id ORDER BY orders_id)"
        ") FROM customers c";

    CHECK(transpile(input) == expected);
}

TEST_CASE("from-first in parenthesised subquery") {
    CHECK(transpile(
        "SELECT { id } FROM t WHERE x IN (FROM t2 SELECT { y })") ==
        "SELECT json_object('id', id) FROM t WHERE x IN "
        "(SELECT json_group_array(json_object('y', y)) FROM t2)");
}

TEST_CASE("mixed from-first and select-first") {
    CHECK(transpile(
        "FROM customers c SELECT { id, "
        "orders: SELECT { total } FROM orders WHERE cid = c.id }") ==
        "SELECT json_object('id', id, 'orders', "
        "(SELECT json_group_array(json_object('total', total)) "
        "FROM orders WHERE cid = c.id)"
        ") FROM customers c");
}

TEST_CASE("from-first renamed field") {
    CHECK(transpile("FROM t SELECT { order_id: id }") ==
          "SELECT json_object('order_id', id) FROM t");
}

TEST_CASE("from-first with inline array") {
    CHECK(transpile("FROM t SELECT { id, tags: [1, 2, 3] }") ==
          "SELECT json_object('id', id, 'tags', json_array(1, 2, 3)) FROM t");
}

TEST_CASE("plain FROM still passes through") {
    CHECK(transpile("SELECT id, name FROM t WHERE x > 0") ==
          "SELECT id, name FROM t WHERE x > 0");
}

TEST_CASE("from-first plain select") {
    CHECK(transpile("FROM t SELECT id, name") ==
          "SELECT id, name FROM t");
}

TEST_CASE("from-first plain select with WHERE and ORDER BY") {
    CHECK(transpile("FROM t WHERE x > 0 ORDER BY y SELECT id, name") ==
          "SELECT id, name FROM t WHERE x > 0 ORDER BY y");
}

TEST_CASE("from-first plain select star") {
    CHECK(transpile("FROM t SELECT *") ==
          "SELECT * FROM t");
}

TEST_CASE("from-first plain nested as scalar subquery") {
    CHECK(transpile(
        "SELECT { id, total: FROM orders WHERE cid = c.id SELECT sum(total) } FROM customers c") ==
        "SELECT json_object('id', id, 'total', "
        "(SELECT sum(total) FROM orders WHERE cid = c.id)"
        ") FROM customers c");
}

TEST_CASE("from-first plain in parenthesised subquery") {
    CHECK(transpile(
        "SELECT id FROM t WHERE x IN (FROM t2 SELECT id)") ==
        "SELECT id FROM t WHERE x IN (SELECT id FROM t2)");
}

TEST_CASE("from-first plain mixed with deep") {
    CHECK(transpile(
        "FROM customers c SELECT { id, total: FROM orders WHERE cid = c.id SELECT sum(total) }") ==
        "SELECT json_object('id', id, 'total', "
        "(SELECT sum(total) FROM orders WHERE cid = c.id)"
        ") FROM customers c");
}

// ── Reverse join (<- syntax) ────────────────────────────────────────

TEST_CASE("single reverse join") {
    CHECK(transpile(
        "SELECT { orders_id, "
        "customer: SELECT { name } FROM o<-customers c "
        "} FROM orders o") ==
        "SELECT json_object('orders_id', orders_id, "
        "'customer', (SELECT json_group_array(json_object('name', name)) "
        "FROM customers c WHERE o.customers_id = c.customers_id)"
        ") FROM orders o");
}

TEST_CASE("reverse join without alias") {
    CHECK(transpile(
        "SELECT { orders_id, "
        "customer: SELECT { name } FROM o<-customers "
        "} FROM orders o") ==
        "SELECT json_object('orders_id', orders_id, "
        "'customer', (SELECT json_group_array(json_object('name', name)) "
        "FROM customers WHERE o.customers_id = customers.customers_id)"
        ") FROM orders o");
}

// ── Join path chains ────────────────────────────────────────────────

TEST_CASE("bridge join (many-to-many)") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "accounts: SELECT { acct_id } FROM c->custacct<-accounts a "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'accounts', (SELECT json_group_array(json_object('acct_id', acct_id)) "
        "FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id "
        "WHERE custacct.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("bridge join with junction alias") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "accounts: SELECT { acct_id } FROM c->custacct ca<-accounts a "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'accounts', (SELECT json_group_array(json_object('acct_id', acct_id)) "
        "FROM custacct ca JOIN accounts a ON ca.accounts_id = a.accounts_id "
        "WHERE ca.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("bridge join with FROM-first syntax") {
    CHECK(transpile(
        "FROM customers c SELECT { customers_id, "
        "accounts: FROM c->custacct<-accounts a SELECT { acct_id } }") ==
        "SELECT json_object('customers_id', customers_id, "
        "'accounts', (SELECT json_group_array(json_object('acct_id', acct_id)) "
        "FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id "
        "WHERE custacct.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("grandchild chain") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "items: SELECT { items_id } FROM c->orders->items "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'items', (SELECT json_group_array(json_object('items_id', items_id)) "
        "FROM orders JOIN items ON items.orders_id = orders.orders_id "
        "WHERE orders.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("grandchild chain with aliases") {
    CHECK(transpile(
        "SELECT { customers_id, "
        "items: SELECT { items_id } FROM c->orders o->items i "
        "} FROM customers c") ==
        "SELECT json_object('customers_id', customers_id, "
        "'items', (SELECT json_group_array(json_object('items_id', items_id)) "
        "FROM orders o JOIN items i ON i.orders_id = o.orders_id "
        "WHERE o.customers_id = c.customers_id)"
        ") FROM customers c");
}

TEST_CASE("three-step chain") {
    CHECK(transpile(
        "SELECT { a_id, "
        "ds: SELECT { d_id } FROM a->b->c->d "
        "} FROM alpha a") ==
        "SELECT json_object('a_id', a_id, "
        "'ds', (SELECT json_group_array(json_object('d_id', d_id)) "
        "FROM b JOIN c ON c.b_id = b.b_id JOIN d ON d.c_id = c.c_id "
        "WHERE b.alpha_id = a.alpha_id)"
        ") FROM alpha a");
}

TEST_CASE("missing table after arrow throws") {
    CHECK_THROWS_AS(transpile(
        "SELECT { id } FROM c-> } FROM customers c"), Error);
}

// ── Singular select (SELECT/1) ─────────────────────────────────────

TEST_CASE("singular object select") {
    CHECK(transpile(
        "SELECT { orders_id, "
        "vendor: SELECT/1 { name } FROM o<-vendors v "
        "} FROM orders o") ==
        "SELECT json_object('orders_id', orders_id, "
        "'vendor', (SELECT json_object('name', name) "
        "FROM vendors v WHERE o.vendors_id = v.vendors_id LIMIT 1)"
        ") FROM orders o");
}

TEST_CASE("singular array select single element") {
    CHECK(transpile(
        "SELECT { id, "
        "top: SELECT/1 [score] FROM scores WHERE uid = u.id ORDER BY score DESC "
        "} FROM users u") ==
        "SELECT json_object('id', id, "
        "'top', (SELECT score "
        "FROM scores WHERE uid = u.id ORDER BY score DESC LIMIT 1)"
        ") FROM users u");
}

TEST_CASE("singular array select multi element") {
    CHECK(transpile(
        "SELECT { id, "
        "pair: SELECT/1 [a, b] FROM pairs WHERE uid = u.id "
        "} FROM users u") ==
        "SELECT json_object('id', id, "
        "'pair', (SELECT json_array(a, b) "
        "FROM pairs WHERE uid = u.id LIMIT 1)"
        ") FROM users u");
}

TEST_CASE("singular from-first") {
    CHECK(transpile(
        "SELECT { orders_id, "
        "vendor: FROM o<-vendors v SELECT/1 { name } "
        "} FROM orders o") ==
        "SELECT json_object('orders_id', orders_id, "
        "'vendor', (SELECT json_object('name', name) "
        "FROM vendors v WHERE o.vendors_id = v.vendors_id LIMIT 1)"
        ") FROM orders o");
}

TEST_CASE("singular top-level") {
    CHECK(transpile("SELECT/1 { id, name } FROM t") ==
          "SELECT json_object('id', id, 'name', name) FROM t LIMIT 1");
}

TEST_CASE("singular in parenthesised subquery") {
    CHECK(transpile(
        "SELECT { id } FROM t WHERE x = (SELECT/1 { y } FROM t2)") ==
        "SELECT json_object('id', id) FROM t WHERE x = "
        "(SELECT json_object('y', y) FROM t2 LIMIT 1)");
}
