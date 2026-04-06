// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Smoke tests: transpile sqldeep syntax, execute against a real SQLite
// database via go-sqlite3, and verify the output. These tests exercise
// the sqlite3_auto_extension registration — importing this package
// should make all sqldeep functions available on every connection.

package sqldeep

import (
	"database/sql"
	"testing"

	_ "github.com/mattn/go-sqlite3"
)

// query transpiles the input, executes it against db, and returns the
// single-column result from the first row.
func query(t *testing.T, db *sql.DB, input string) string {
	t.Helper()
	sql, err := Transpile(input)
	if err != nil {
		t.Fatalf("Transpile(%q): %v", input, err)
	}
	var result string
	if err := db.QueryRow(sql).Scan(&result); err != nil {
		t.Fatalf("QueryRow(%q): %v\nSQL: %s", input, err, sql)
	}
	return result
}

func openDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite3", ":memory:")
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func exec(t *testing.T, db *sql.DB, sql string) {
	t.Helper()
	if _, err := db.Exec(sql); err != nil {
		t.Fatalf("Exec(%q): %v", sql, err)
	}
}

func TestSmoke_BasicObject(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER, name TEXT)")
	exec(t, db, "INSERT INTO t VALUES(1, 'alice')")

	got := query(t, db, "SELECT { id, name } FROM t")
	want := `{"id":1,"name":"alice"}`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_NestedSubquery(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE customers(customers_id INTEGER, name TEXT)")
	exec(t, db, "CREATE TABLE orders(orders_id INTEGER, customers_id INTEGER, total REAL)")
	exec(t, db, "INSERT INTO customers VALUES(1, 'alice')")
	exec(t, db, "INSERT INTO orders VALUES(10, 1, 99.50)")
	exec(t, db, "INSERT INTO orders VALUES(11, 1, 42.00)")

	got := query(t, db, `
		SELECT { customers_id, name,
			orders: SELECT { orders_id, total }
				FROM orders WHERE customers_id = c.customers_id
				ORDER BY orders_id,
		} FROM customers c`)
	want := `{"customers_id":1,"name":"alice","orders":[{"orders_id":10,"total":99.5},{"orders_id":11,"total":42.0}]}`
	if got != want {
		t.Errorf("got  %q\nwant %q", got, want)
	}
}

func TestSmoke_InlineArray(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER)")
	exec(t, db, "INSERT INTO t VALUES(1)")

	got := query(t, db, "SELECT { id, tags: [10, 20, 30] } FROM t")
	want := `{"id":1,"tags":[10,20,30]}`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_SingularSelect(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER, name TEXT)")
	exec(t, db, "INSERT INTO t VALUES(1, 'alice')")

	got := query(t, db, "SELECT/1 { id, name } FROM t")
	want := `{"id":1,"name":"alice"}`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_QualifiedBareField(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER)")
	exec(t, db, "CREATE TABLE s(id INTEGER, repo TEXT)")
	exec(t, db, "INSERT INTO t VALUES(1)")
	exec(t, db, "INSERT INTO s VALUES(1, 'sqldeep')")

	got := query(t, db, "SELECT { t.id, s.repo } FROM t JOIN s ON s.id = t.id")
	want := `{"id":1,"repo":"sqldeep"}`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_JsonBoolean(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER)")
	exec(t, db, "INSERT INTO t VALUES(1)")

	got := query(t, db, "SELECT { id, active: true } FROM t")
	want := `{"id":1,"active":true}`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_XMLElement(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(name TEXT)")
	exec(t, db, "INSERT INTO t VALUES('alice')")

	got := query(t, db, `SELECT <div class="card">{name}</div> FROM t`)
	want := `<div class="card">alice</div>`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_JSONML(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(name TEXT)")
	exec(t, db, "INSERT INTO t VALUES('alice')")

	got := query(t, db, `SELECT jsonml(<span>{name}</span>) FROM t`)
	want := `["span","alice"]`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_JSX(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(x INTEGER, y INTEGER)")
	exec(t, db, "INSERT INTO t VALUES(10, 20)")

	got := query(t, db, `SELECT jsx(<Point data={{x, y}}/>) FROM t`)
	want := `["Point",{"data":{"x":10,"y":20}}]`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestSmoke_ViewRecomposition(t *testing.T) {
	db := openDB(t)
	exec(t, db, "CREATE TABLE t(id INTEGER, name TEXT)")
	exec(t, db, "INSERT INTO t VALUES(1, 'alice')")

	// Create view with JSONML — BLOB survives the view.
	createSQL, err := Transpile(`CREATE VIEW v AS SELECT jsonml(<b>{name}</b>) AS col FROM t`)
	if err != nil {
		t.Fatalf("Transpile CREATE VIEW: %v", err)
	}
	exec(t, db, createSQL)

	// Query the view — BLOB is consumed by outer JSONML element.
	got := query(t, db, `SELECT jsonml(<div>{(SELECT col FROM v)}</div>)`)
	want := `["div",["b","alice"]]`
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}
