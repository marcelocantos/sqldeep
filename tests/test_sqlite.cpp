// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Integration tests: transpile sqldeep syntax, execute the resulting SQL
// against an in-memory SQLite database, and verify the JSON output.

#include <doctest.h>
#include <sqlite3.h>
#include "sqldeep.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// RAII wrapper for sqlite3*.
struct DbGuard {
    sqlite3* db = nullptr;
    DbGuard() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            throw std::runtime_error("failed to open :memory: db");
    }
    ~DbGuard() { if (db) sqlite3_close(db); }
    DbGuard(const DbGuard&) = delete;
    DbGuard& operator=(const DbGuard&) = delete;
};

// Execute SQL that returns no rows.
void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("exec failed: " + msg + "\nSQL: " + sql);
    }
}

// Execute a query and return all result rows as strings (one string per row).
std::vector<std::string> query(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(
            std::string("prepare failed: ") + sqlite3_errmsg(db) +
            "\nSQL: " + sql);

    std::vector<std::string> rows;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 0));
        rows.push_back(text ? text : "NULL");
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
        throw std::runtime_error(
            std::string("step failed: ") + sqlite3_errmsg(db));
    return rows;
}

// Transpile and execute, returning result rows.
std::vector<std::string> transpile_and_query(sqlite3* db,
                                              const std::string& deep_sql) {
    std::string sql = sqldeep::transpile(deep_sql);
    return query(db, sql);
}

} // namespace

// ── Single-table tests ──────────────────────────────────────────────

TEST_CASE("sqlite: basic object select") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");
    exec(g.db, "INSERT INTO t VALUES(2, 'bob')");

    auto rows = transpile_and_query(g.db,
        "SELECT { id, name } FROM t ORDER BY id");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"id":1,"name":"alice"})");
    CHECK(rows[1] == R"({"id":2,"name":"bob"})");
}

TEST_CASE("sqlite: renamed field") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'x')");

    auto rows = transpile_and_query(g.db,
        "SELECT { item_id: id, value: val } FROM t");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"item_id":1,"value":"x"})");
}

TEST_CASE("sqlite: inline array") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY)");
    exec(g.db, "INSERT INTO t VALUES(1)");

    auto rows = transpile_and_query(g.db,
        "SELECT { id, tags: [10, 20, 30] } FROM t");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"id":1,"tags":[10,20,30]})");
}

TEST_CASE("sqlite: expression with function call") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, a TEXT, b TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, NULL, 'fallback')");

    auto rows = transpile_and_query(g.db,
        "SELECT { id, v: coalesce(a, b) } FROM t");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"id":1,"v":"fallback"})");
}

// ── Nested subquery tests ───────────────────────────────────────────

TEST_CASE("sqlite: two-level object nesting") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cid INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            id, name,
            orders: SELECT { order_id: id, total } FROM orders
                    WHERE cid = c.id ORDER BY id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"id":1,"name":"alice","orders":[{"order_id":10,"total":99.5},{"order_id":11,"total":42.0}]})");
}

TEST_CASE("sqlite: two-level array subquery") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cid INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            name,
            totals: SELECT [total] FROM orders
                    WHERE cid = c.id ORDER BY id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"name":"alice","totals":[99.5,42.0]})");
}

TEST_CASE("sqlite: three-level nesting") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, pid INTEGER);
        CREATE TABLE items(id INTEGER PRIMARY KEY, oid INTEGER, name TEXT, qty INTEGER);
        INSERT INTO people VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1);
        INSERT INTO items VALUES(100, 10, 'widget', 3);
        INSERT INTO items VALUES(101, 10, 'gadget', 1);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            id, name,
            orders: SELECT {
                order_id: id,
                items: SELECT {
                    item: name,
                    qty,
                } FROM items i WHERE oid = o.id ORDER BY i.id,
            } FROM orders o WHERE pid = p.id ORDER BY o.id,
        } FROM people p
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"id":1,"name":"alice","orders":[{"order_id":10,"items":[{"item":"widget","qty":3},{"item":"gadget","qty":1}]}]})");
}

// ── Mixed nesting ───────────────────────────────────────────────────

TEST_CASE("sqlite: mixed array and object nesting") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cid INTEGER);
        CREATE TABLE items(id INTEGER PRIMARY KEY, oid INTEGER, name TEXT);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1);
        INSERT INTO items VALUES(100, 10, 'thing');
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            id,
            tags: [1, 2, 3],
            orders: SELECT {
                order_id: id,
                item_names: SELECT [name] FROM items WHERE oid = o.id,
            } FROM orders o WHERE cid = c.id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"id":1,"tags":[1,2,3],"orders":[{"order_id":10,"item_names":["thing"]}]})");
}

// ── Empty subquery results ──────────────────────────────────────────

TEST_CASE("sqlite: empty subquery produces null") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cid INTEGER);
        INSERT INTO customers VALUES(1, 'alice');
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            name,
            orders: SELECT { order_id: id } FROM orders WHERE cid = c.id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    // json_group_array on zero rows returns empty array
    CHECK(rows[0] == R"({"name":"alice","orders":[]})");
}

// ── WHERE with expressions ──────────────────────────────────────────

TEST_CASE("sqlite: WHERE clause preserved") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, active INTEGER);
        INSERT INTO t VALUES(1, 'alice', 1);
        INSERT INTO t VALUES(2, 'bob', 0);
        INSERT INTO t VALUES(3, 'carol', 1);
    )");

    auto rows = transpile_and_query(g.db,
        "SELECT { id, name } FROM t WHERE active = 1 ORDER BY id");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"id":1,"name":"alice"})");
    CHECK(rows[1] == R"({"id":3,"name":"carol"})");
}

// ── Key escaping ───────────────────────────────────────────────────

TEST_CASE("sqlite: single-quote in key") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'x')");

    auto rows = transpile_and_query(g.db,
        "SELECT { \"it's\": val } FROM t");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"it's":"x"})");
}

TEST_CASE("sqlite: SQL doubled-quote string") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY)");
    exec(g.db, "INSERT INTO t VALUES(1)");

    auto rows = transpile_and_query(g.db,
        "SELECT { name: 'O''Brien' } FROM t");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"name":"O'Brien"})");
}

// ── SQL passthrough ─────────────────────────────────────────────────

// ── Auto-join (-> syntax) ──────────────────────────────────────────

TEST_CASE("sqlite: two-level auto-join") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            customers_id, name,
            orders: SELECT { orders_id, total }
                    FROM c->orders ORDER BY orders_id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","orders":[{"orders_id":10,"total":99.5},{"orders_id":11,"total":42.0}]})");
}

TEST_CASE("sqlite: three-level auto-join") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER);
        CREATE TABLE items(items_id INTEGER PRIMARY KEY, orders_id INTEGER, name TEXT, qty INTEGER);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1);
        INSERT INTO items VALUES(100, 10, 'widget', 3);
        INSERT INTO items VALUES(101, 10, 'gadget', 1);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            customers_id, name,
            orders: SELECT {
                orders_id,
                items: SELECT { name, qty }
                       FROM o->items ORDER BY items_id,
            } FROM c->orders o ORDER BY orders_id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","orders":[{"orders_id":10,"items":[{"name":"widget","qty":3},{"name":"gadget","qty":1}]}]})");
}

// ── FROM-first syntax ───────────────────────────────────────────────

TEST_CASE("sqlite: from-first basic") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");
    exec(g.db, "INSERT INTO t VALUES(2, 'bob')");

    auto rows = transpile_and_query(g.db,
        "FROM t ORDER BY id SELECT { id, name }");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"id":1,"name":"alice"})");
    CHECK(rows[1] == R"({"id":2,"name":"bob"})");
}

TEST_CASE("sqlite: from-first nested") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cid INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM customers c SELECT {
            id, name,
            orders: FROM orders o WHERE o.cid = c.id ORDER BY o.id SELECT { order_id: id, total },
        }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"id":1,"name":"alice","orders":[{"order_id":10,"total":99.5},{"order_id":11,"total":42.0}]})");
}

TEST_CASE("sqlite: from-first with auto-join") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM customers c SELECT {
            customers_id, name,
            orders: FROM c->orders ORDER BY orders_id SELECT { orders_id, total },
        }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","orders":[{"orders_id":10,"total":99.5},{"orders_id":11,"total":42.0}]})");
}

TEST_CASE("sqlite: from-first three-level") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER);
        CREATE TABLE items(items_id INTEGER PRIMARY KEY, orders_id INTEGER, name TEXT, qty INTEGER);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1);
        INSERT INTO items VALUES(100, 10, 'widget', 3);
        INSERT INTO items VALUES(101, 10, 'gadget', 1);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM customers c SELECT {
            customers_id, name,
            orders: FROM c->orders o ORDER BY orders_id SELECT {
                orders_id,
                items: FROM o->items ORDER BY items_id SELECT { name, qty },
            },
        }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","orders":[{"orders_id":10,"items":[{"name":"widget","qty":3},{"name":"gadget","qty":1}]}]})");
}

// ── SQL passthrough ─────────────────────────────────────────────────

TEST_CASE("sqlite: plain SQL passthrough") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");

    // No deep constructs — should pass through and work normally
    auto rows = query(g.db, sqldeep::transpile("SELECT id, name FROM t"));

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == "1");
}
