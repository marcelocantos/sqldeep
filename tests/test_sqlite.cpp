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

// Transpile via C API and execute, returning result rows.
std::vector<std::string> transpile_and_query(sqlite3* db,
                                              const std::string& deep_sql) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* result = sqldeep_transpile(deep_sql.c_str(),
                                      &err_msg, &err_line, &err_col);
    if (!result) {
        std::string msg = err_msg ? err_msg : "unknown error";
        if (err_msg) sqldeep_free(err_msg);
        throw std::runtime_error("transpile failed: " + msg);
    }
    std::string sql(result);
    sqldeep_free(result);
    return query(db, sql);
}

// Transpile with FK info via C API and execute.
std::vector<std::string> transpile_and_query(
        sqlite3* db, const std::string& deep_sql,
        const sqldeep_foreign_key* fks, int fk_count) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* result = sqldeep_transpile_fk(deep_sql.c_str(), fks, fk_count,
                                         &err_msg, &err_line, &err_col);
    if (!result) {
        std::string msg = err_msg ? err_msg : "unknown error";
        if (err_msg) sqldeep_free(err_msg);
        throw std::runtime_error("transpile failed: " + msg);
    }
    std::string sql(result);
    sqldeep_free(result);
    return query(db, sql);
}

// ── XML function implementations for integration testing ────────────
//
// These are minimal implementations of xml_element, xml_attrs, and xml_agg
// sufficient for verifying that transpiled XML queries produce correct output.
// Production implementations would live in sqlpipe.
//
// Sentinel approach: XML output is prefixed with '\x01' so xml_element can
// distinguish "already-XML" children (pass through) from plain text (escape).

static const char kXmlSentinel = '\x01';

static bool is_xml_sentinel(const char* s) {
    return s && s[0] == kXmlSentinel;
}

static std::string xml_escape_text(const char* s) {
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default:  out += *s; break;
        }
    }
    return out;
}

static std::string xml_escape_attr(const char* s) {
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
        case '"': out += "&quot;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default:  out += *s; break;
        }
    }
    return out;
}

// xml_attrs(name1, value1, name2, value2, ...)
static void sqlite_xml_attrs(sqlite3_context* ctx, int argc,
                              sqlite3_value** argv) {
    if (argc % 2 != 0) {
        sqlite3_result_error(ctx, "xml_attrs requires even number of args", -1);
        return;
    }
    std::string out;
    for (int i = 0; i < argc; i += 2) {
        if (sqlite3_value_type(argv[i + 1]) == SQLITE_NULL) continue;
        const char* name = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[i]));
        int vtype = sqlite3_value_type(argv[i + 1]);
        if (vtype == SQLITE_INTEGER) {
            int v = sqlite3_value_int(argv[i + 1]);
            if (v == 1) {
                out += " ";
                out += name;
            }
            // v == 0: omit
        } else {
            const char* val = reinterpret_cast<const char*>(
                sqlite3_value_text(argv[i + 1]));
            out += " ";
            out += name;
            out += "=\"";
            out += xml_escape_attr(val);
            out += "\"";
        }
    }
    std::string result = std::string(1, kXmlSentinel) + out;
    sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
}

// xml_element(tag, [attrs], ...children)
static void sqlite_xml_element(sqlite3_context* ctx, int argc,
                                sqlite3_value** argv) {
    if (argc < 1) {
        sqlite3_result_error(ctx, "xml_element requires at least 1 arg", -1);
        return;
    }
    const char* tag = reinterpret_cast<const char*>(
        sqlite3_value_text(argv[0]));
    std::string attrs_str;
    int child_start = 1;

    // Check if first arg after tag is xml_attrs result.
    // xml_attrs output starts with sentinel + space (e.g. "\x01 class=\"x\"").
    // xml_element output starts with sentinel + '<'.
    if (argc > 1) {
        const char* a = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[1]));
        if (is_xml_sentinel(a) && a[1] == ' ') {
            attrs_str = a + 1; // strip sentinel, keep leading space
            child_start = 2;
        }
    }

    // Collect children
    std::string children;
    bool has_children = false;
    for (int i = child_start; i < argc; ++i) {
        if (sqlite3_value_type(argv[i]) == SQLITE_NULL) continue;
        has_children = true;
        const char* c = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[i]));
        if (is_xml_sentinel(c)) {
            children += c + 1; // already XML, strip sentinel
        } else {
            children += xml_escape_text(c);
        }
    }

    std::string out;
    out += kXmlSentinel;
    out += "<";
    out += tag;
    out += attrs_str;
    if (has_children) {
        out += ">";
        out += children;
        out += "</";
        out += tag;
        out += ">";
    } else {
        out += "/>";
    }

    sqlite3_result_text(ctx, out.c_str(), -1, SQLITE_TRANSIENT);
}

// xml_agg: aggregate that concatenates XML fragments, preserving sentinel.
struct XmlAggCtx {
    std::string accum;
};

static void sqlite_xml_agg_step(sqlite3_context* ctx, int /*argc*/,
                                 sqlite3_value** argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) return;
    auto** pp = reinterpret_cast<XmlAggCtx**>(
        sqlite3_aggregate_context(ctx, sizeof(XmlAggCtx*)));
    if (!*pp) *pp = new XmlAggCtx();
    const char* v = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    if (is_xml_sentinel(v)) {
        (*pp)->accum += v + 1;
    } else {
        (*pp)->accum += xml_escape_text(v);
    }
}

static void sqlite_xml_agg_final(sqlite3_context* ctx) {
    auto** pp = reinterpret_cast<XmlAggCtx**>(
        sqlite3_aggregate_context(ctx, 0));
    if (!pp || !*pp) {
        sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
        return;
    }
    std::string result = std::string(1, kXmlSentinel) + (*pp)->accum;
    sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
    delete *pp;
}

static void register_xml_functions(sqlite3* db) {
    sqlite3_create_function(db, "xml_element", -1, SQLITE_UTF8,
                            nullptr, sqlite_xml_element, nullptr, nullptr);
    sqlite3_create_function(db, "xml_attrs", -1, SQLITE_UTF8,
                            nullptr, sqlite_xml_attrs, nullptr, nullptr);
    sqlite3_create_function(db, "xml_agg", 1, SQLITE_UTF8,
                            nullptr, nullptr,
                            sqlite_xml_agg_step, sqlite_xml_agg_final);
}

// DbGuard variant with XML functions registered.
struct DbGuardXml {
    sqlite3* db = nullptr;
    DbGuardXml() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            throw std::runtime_error("failed to open :memory: db");
        register_xml_functions(db);
    }
    ~DbGuardXml() { if (db) sqlite3_close(db); }
    DbGuardXml(const DbGuardXml&) = delete;
    DbGuardXml& operator=(const DbGuardXml&) = delete;
};

// Query that strips the sentinel prefix from the result.
std::string xml_query(sqlite3* db, const std::string& deep_sql) {
    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* result = sqldeep_transpile(deep_sql.c_str(),
                                      &err_msg, &err_line, &err_col);
    if (!result) {
        std::string msg = err_msg ? err_msg : "unknown error";
        if (err_msg) sqldeep_free(err_msg);
        throw std::runtime_error("transpile failed: " + msg);
    }
    std::string sql(result);
    sqldeep_free(result);
    auto rows = query(db, sql);
    // Strip sentinel from results
    for (auto& row : rows) {
        if (!row.empty() && row[0] == kXmlSentinel)
            row = row.substr(1);
    }
    if (rows.size() != 1)
        throw std::runtime_error("expected 1 row, got " +
                                  std::to_string(rows.size()));
    return rows[0];
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

// ── Reverse join (<- syntax) ────────────────────────────────────────

TEST_CASE("sqlite: reverse join (many-to-one)") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER, total REAL);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO customers VALUES(2, 'bob');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 2, 42.0);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            orders_id, total,
            customer: SELECT { name }
                      FROM o<-customers c,
        } FROM orders o ORDER BY orders_id
    )");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] ==
        R"({"orders_id":10,"total":99.5,"customer":[{"name":"alice"}]})");
    CHECK(rows[1] ==
        R"({"orders_id":11,"total":42.0,"customer":[{"name":"bob"}]})");
}

// ── Grandchild chain (-> -> ) ───────────────────────────────────────

TEST_CASE("sqlite: grandchild chain") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER);
        CREATE TABLE items(items_id INTEGER PRIMARY KEY, orders_id INTEGER, name TEXT);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1);
        INSERT INTO orders VALUES(11, 1);
        INSERT INTO items VALUES(100, 10, 'widget');
        INSERT INTO items VALUES(101, 10, 'gadget');
        INSERT INTO items VALUES(102, 11, 'doohickey');
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            customers_id, name,
            items: SELECT { items_id, name }
                   FROM c->orders o->items i,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    // Verify all three items appear (ordering depends on SQLite JOIN traversal)
    auto& r = rows[0];
    CHECK(r.find("\"customers_id\":1") != std::string::npos);
    CHECK(r.find("\"name\":\"alice\"") != std::string::npos);
    CHECK(r.find("\"name\":\"widget\"") != std::string::npos);
    CHECK(r.find("\"name\":\"gadget\"") != std::string::npos);
    CHECK(r.find("\"name\":\"doohickey\"") != std::string::npos);
}

// ── Bridge join (many-to-many) ──────────────────────────────────────

TEST_CASE("sqlite: bridge join (many-to-many)") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE custacct(custacct_id INTEGER PRIMARY KEY,
                              customers_id INTEGER, accounts_id INTEGER);
        CREATE TABLE accounts(accounts_id INTEGER PRIMARY KEY, acct_name TEXT);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO accounts VALUES(100, 'savings');
        INSERT INTO accounts VALUES(101, 'checking');
        INSERT INTO custacct VALUES(1, 1, 100);
        INSERT INTO custacct VALUES(2, 1, 101);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            customers_id, name,
            accounts: SELECT { acct_name }
                      FROM c->custacct<-accounts a
                      ORDER BY a.accounts_id,
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","accounts":[{"acct_name":"savings"},{"acct_name":"checking"}]})");
}

TEST_CASE("sqlite: bridge join with FROM-first") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE custacct(custacct_id INTEGER PRIMARY KEY,
                              customers_id INTEGER, accounts_id INTEGER);
        CREATE TABLE accounts(accounts_id INTEGER PRIMARY KEY, acct_name TEXT);
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO accounts VALUES(100, 'savings');
        INSERT INTO accounts VALUES(101, 'checking');
        INSERT INTO custacct VALUES(1, 1, 100);
        INSERT INTO custacct VALUES(2, 1, 101);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM customers c SELECT {
            customers_id, name,
            accounts: FROM c->custacct<-accounts a ORDER BY a.accounts_id
                      SELECT { acct_name },
        }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] ==
        R"({"customers_id":1,"name":"alice","accounts":[{"acct_name":"savings"},{"acct_name":"checking"}]})");
}

// ── Singular select (SELECT/1) ───────────────────────────────────────

TEST_CASE("sqlite: singular object select") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, vendors_id INTEGER);
        CREATE TABLE vendors(vendors_id INTEGER PRIMARY KEY, name TEXT);
        INSERT INTO vendors VALUES(1, 'Acme');
        INSERT INTO orders VALUES(10, 1);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            orders_id,
            vendor: SELECT/1 { name } FROM o<-vendors v,
        } FROM orders o
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"orders_id":10,"vendor":{"name":"Acme"}})");
}

TEST_CASE("sqlite: singular returns null for zero rows") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, vendors_id INTEGER);
        CREATE TABLE vendors(vendors_id INTEGER PRIMARY KEY, name TEXT);
        INSERT INTO orders VALUES(10, 999);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            orders_id,
            vendor: SELECT/1 { name } FROM o<-vendors v,
        } FROM orders o
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"orders_id":10,"vendor":null})");
}

// ── Aggregate field (SELECT expr, no FROM) ──────────────────────────

TEST_CASE("sqlite: aggregate field collects group values") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE custacct(customer_id INTEGER, account_id INTEGER);
        INSERT INTO custacct VALUES(1, 100);
        INSERT INTO custacct VALUES(1, 200);
        INSERT INTO custacct VALUES(2, 100);
        INSERT INTO custacct VALUES(2, 200);
        INSERT INTO custacct VALUES(3, 300);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM (
            FROM custacct a GROUP BY a.customer_id
            SELECT a.customer_id,
                (SELECT group_concat(account_id)
                 FROM (SELECT account_id FROM custacct b
                       WHERE b.customer_id = a.customer_id
                       ORDER BY account_id)) AS acct_sig
        ) c
        GROUP BY c.acct_sig HAVING count(*) > 1
        SELECT {
            accounts: c.acct_sig,
            customers: SELECT c.customer_id,
        }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"accounts":"100,200","customers":[1,2]})");
}

TEST_CASE("sqlite: aggregate field singular returns last value") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(grp TEXT, val INTEGER);
        INSERT INTO t VALUES('a', 1);
        INSERT INTO t VALUES('a', 2);
        INSERT INTO t VALUES('b', 3);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM t GROUP BY grp ORDER BY grp
        SELECT {
            grp,
            total: SELECT/1 sum(val),
        }
    )");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"grp":"a","total":3})");
    CHECK(rows[1] == R"({"grp":"b","total":3})");
}

TEST_CASE("sqlite: aggregate field multiple in one object") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(grp TEXT, name TEXT, val INTEGER);
        INSERT INTO t VALUES('x', 'alice', 10);
        INSERT INTO t VALUES('x', 'bob', 20);
        INSERT INTO t VALUES('y', 'carol', 30);
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM t GROUP BY grp ORDER BY grp
        SELECT {
            grp,
            names: SELECT name,
            total: SELECT/1 sum(val),
        }
    )");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"grp":"x","names":["alice","bob"],"total":30})");
    CHECK(rows[1] == R"({"grp":"y","names":["carol"],"total":30})");
}

TEST_CASE("sqlite: aggregate field with function expression") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(grp TEXT, name TEXT);
        INSERT INTO t VALUES('a', 'hello');
        INSERT INTO t VALUES('a', 'world');
    )");

    auto rows = transpile_and_query(g.db, R"(
        FROM t WHERE grp = 'a' GROUP BY grp
        SELECT { grp, labels: SELECT upper(name) }
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"grp":"a","labels":["HELLO","WORLD"]})");
}

// ── Computed keys ────────────────────────────────────────────────────

TEST_CASE("sqlite: computed key builds dynamic JSON keys") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(k TEXT, v INTEGER);
        INSERT INTO t VALUES('x', 1);
        INSERT INTO t VALUES('y', 2);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT { ('key_' || k): v } FROM t ORDER BY k
    )");

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"key_x":1})");
    CHECK(rows[1] == R"({"key_y":2})");
}

TEST_CASE("sqlite: computed key mixed with static keys") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE t(tag TEXT, name TEXT, score INTEGER);
        INSERT INTO t VALUES('rank', 'alice', 10);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT { name, (tag): score } FROM t
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"name":"alice","rank":10})");
}

// ── Plain FROM-first ─────────────────────────────────────────────────

TEST_CASE("sqlite: plain from-first") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");
    exec(g.db, "INSERT INTO t VALUES(2, 'bob')");

    auto rows = transpile_and_query(g.db,
        "FROM t ORDER BY id SELECT id, name");

    REQUIRE(rows.size() == 2);
    // Plain SELECT returns columns, not JSON
    CHECK(rows[0] == "1");
    CHECK(rows[1] == "2");
}

TEST_CASE("sqlite: plain from-first as scalar subquery") {
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
            total: FROM orders WHERE cid = c.id SELECT sum(total),
        } FROM customers c
    )");

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"name":"alice","total":141.5})");
}

// ── SQL passthrough ─────────────────────────────────────────────────

TEST_CASE("sqlite: plain SQL passthrough") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");

    // No deep constructs — should pass through and work normally
    char* result = sqldeep_transpile("SELECT id, name FROM t",
                                      nullptr, nullptr, nullptr);
    REQUIRE(result);
    auto rows = query(g.db, result);
    sqldeep_free(result);

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == "1");
}

// ── JSON arrow operators ────────────────────────────────────────────

TEST_CASE("sqlite: json ->> operator passthrough") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, data TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, '{\"name\":\"alice\"}')");

    auto rows = transpile_and_query(g.db,
        "SELECT { id, name: data->>'name' } FROM t");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"id":1,"name":"alice"})");
}

TEST_CASE("sqlite: json -> operator in WHERE") {
    DbGuard g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, data TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, '{\"type\":\"a\"}')");
    exec(g.db, "INSERT INTO t VALUES(2, '{\"type\":\"b\"}')");

    auto rows = transpile_and_query(g.db,
        "SELECT { id } FROM t WHERE data->>'type' = 'a'");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"id":1})");
}

// ── Recursive select ────────────────────────────────────────────────

TEST_CASE("sqlite: recursive tree") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE categories(id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER);
        INSERT INTO categories VALUES(1, 'Root', NULL);
        INSERT INTO categories VALUES(2, 'A', 1);
        INSERT INTO categories VALUES(3, 'B', 1);
        INSERT INTO categories VALUES(4, 'A1', 2);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT/1 { id, name, children: * }
        FROM categories
        RECURSE ON (parent_id)
        WHERE parent_id IS NULL
    )");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"id":1,"name":"Root","children":[{"id":2,"name":"A","children":[{"id":4,"name":"A1","children":[]}]},{"id":3,"name":"B","children":[]}]})");
}

TEST_CASE("sqlite: recursive forest") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE categories(id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER);
        INSERT INTO categories VALUES(1, 'Root1', NULL);
        INSERT INTO categories VALUES(2, 'Root2', NULL);
        INSERT INTO categories VALUES(3, 'Child1', 1);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT { id, name, children: * }
        FROM categories
        RECURSE ON (parent_id)
        WHERE parent_id IS NULL
    )");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"([{"id":1,"name":"Root1","children":[{"id":3,"name":"Child1","children":[]}]},{"id":2,"name":"Root2","children":[]}])");
}

TEST_CASE("sqlite: recursive empty tree") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE categories(id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER);
    )");

    auto rows = transpile_and_query(g.db, R"(
        SELECT/1 { id, name, children: * }
        FROM categories
        RECURSE ON (parent_id)
        WHERE parent_id IS NULL
    )");
    REQUIRE(rows.size() == 1);
    // group_concat on zero rows returns NULL
    CHECK(rows[0] == "NULL");
}

// ── FK-guided joins ─────────────────────────────────────────────────

TEST_CASE("sqlite: fk-guided join") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, cust_id INTEGER, total REAL,
                            FOREIGN KEY(cust_id) REFERENCES customers(id));
        INSERT INTO customers VALUES(1, 'alice');
        INSERT INTO orders VALUES(10, 1, 99.5);
        INSERT INTO orders VALUES(11, 1, 42.0);
    )");

    sqldeep_column_pair cols[] = {{"cust_id", "id"}};
    sqldeep_foreign_key fks[] = {{"orders", "customers", cols, 1}};
    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            name,
            orders: FROM c->orders o ORDER BY o.id SELECT { total },
        } FROM customers c
    )", fks, 1);

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == R"({"name":"alice","orders":[{"total":99.5},{"total":42.0}]})");
}

TEST_CASE("sqlite: fk-guided multi-column join") {
    DbGuard g;
    exec(g.db, R"(
        CREATE TABLE regions(region TEXT, shop TEXT, name TEXT,
                             PRIMARY KEY(region, shop));
        CREATE TABLE sales(id INTEGER PRIMARY KEY,
                           region TEXT, shop TEXT, amount REAL);
        INSERT INTO regions VALUES('east', 'a', 'East A');
        INSERT INTO regions VALUES('east', 'b', 'East B');
        INSERT INTO sales VALUES(1, 'east', 'a', 100.0);
        INSERT INTO sales VALUES(2, 'east', 'a', 200.0);
        INSERT INTO sales VALUES(3, 'east', 'b', 50.0);
    )");

    sqldeep_column_pair cols[] = {{"region", "region"}, {"shop", "shop"}};
    sqldeep_foreign_key fks[] = {{"sales", "regions", cols, 2}};
    auto rows = transpile_and_query(g.db, R"(
        SELECT {
            name,
            sales: FROM r->sales s ORDER BY s.id SELECT { amount },
        } FROM regions r ORDER BY r.region, r.shop
    )", fks, 1);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == R"({"name":"East A","sales":[{"amount":100.0},{"amount":200.0}]})");
    CHECK(rows[1] == R"({"name":"East B","sales":[{"amount":50.0}]})");
}

// ── XML integration tests ──────────────────────────────────────────

TEST_CASE("sqlite: xml static element") {
    DbGuardXml g;
    auto result = xml_query(g.db, "SELECT <div>hello</div>");
    CHECK(result == "<div>hello</div>");
}

TEST_CASE("sqlite: xml with attributes") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, cls TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'highlight')");
    auto result = xml_query(g.db,
        "SELECT <span class={cls}>text</span> FROM t");
    CHECK(result == R"(<span class="highlight">text</span>)");
}

TEST_CASE("sqlite: xml interpolation") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");
    auto result = xml_query(g.db,
        "SELECT <td>{name}</td> FROM t");
    CHECK(result == "<td>alice</td>");
}

TEST_CASE("sqlite: xml escaping") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, '<b>bold</b>')");
    auto result = xml_query(g.db,
        "SELECT <td>{val}</td> FROM t");
    CHECK(result == "<td>&lt;b&gt;bold&lt;/b&gt;</td>");
}

TEST_CASE("sqlite: xml nested elements") {
    DbGuardXml g;
    auto result = xml_query(g.db,
        "SELECT <div><span>inner</span></div>");
    CHECK(result == "<div><span>inner</span></div>");
}

TEST_CASE("sqlite: xml self-closing") {
    DbGuardXml g;
    auto result = xml_query(g.db, "SELECT <br/>");
    CHECK(result == "<br/>");
}

TEST_CASE("sqlite: xml boolean attribute") {
    DbGuardXml g;
    auto result = xml_query(g.db, "SELECT <input disabled/>");
    CHECK(result == "<input disabled/>");
}

TEST_CASE("sqlite: xml subquery") {
    DbGuardXml g;
    exec(g.db, R"(
        CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT);
        INSERT INTO items VALUES(1, 'apple');
        INSERT INTO items VALUES(2, 'banana');
    )");
    auto result = xml_query(g.db,
        "SELECT <ul>{SELECT <li>{name}</li> FROM items ORDER BY id}</ul>");
    CHECK(result == "<ul><li>apple</li><li>banana</li></ul>");
}

TEST_CASE("sqlite: xml inside json") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, 'alice')");
    auto rows = transpile_and_query(g.db,
        "SELECT { name, badge: <b>{name}</b> } FROM t");
    REQUIRE(rows.size() == 1);
    // The sentinel prefix leaks into the JSON value — this is expected
    // with the sentinel approach. A production implementation would strip
    // it at the presentation boundary or use SQLite subtypes instead.
    auto result = rows[0];
    // Just verify the JSON structure is present and contains the XML
    CHECK(result.find("\"name\":\"alice\"") != std::string::npos);
    CHECK(result.find("<b>alice</b>") != std::string::npos);
}

TEST_CASE("sqlite: xml mixed text and interpolation") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE t(id INTEGER PRIMARY KEY, amt TEXT)");
    exec(g.db, "INSERT INTO t VALUES(1, '42')");
    auto result = xml_query(g.db,
        "SELECT <td>Price: {amt}</td> FROM t");
    CHECK(result == "<td>Price: 42</td>");
}

TEST_CASE("sqlite: xml empty subquery") {
    DbGuardXml g;
    exec(g.db, "CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT)");
    auto result = xml_query(g.db,
        "SELECT <ul>{SELECT <li>{name}</li> FROM items}</ul>");
    CHECK(result == "<ul></ul>");
}
