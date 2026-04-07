// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Data-driven SQLite integration tests: load test cases from
// testdata/sqlite.yaml, transpile, execute, and verify.

package sqldeep

import (
	"database/sql"
	"os"
	"strings"
	"testing"

	_ "github.com/mattn/go-sqlite3"
	"gopkg.in/yaml.v3"
)

type sqliteTestCase struct {
	Name             string         `yaml:"name"`
	Setup            []string       `yaml:"setup"`
	Input            string         `yaml:"input"`
	RawSQL           string         `yaml:"raw_sql"`
	Expected         []string       `yaml:"expected"`
	ExpectedContains []string       `yaml:"expected_contains"`
	FKs              []yamlFK       `yaml:"fks"`
	Steps            []sqliteStep   `yaml:"steps"`
}

type sqliteStep struct {
	TranspileExec  string `yaml:"transpile_exec"`
	TranspileQuery string `yaml:"transpile_query"`
}

func loadSQLiteTests(t *testing.T) []sqliteTestCase {
	t.Helper()
	data, err := os.ReadFile("../../testdata/sqlite.yaml")
	if err != nil {
		t.Fatalf("reading sqlite.yaml: %v", err)
	}
	var tests []sqliteTestCase
	if err := yaml.Unmarshal(data, &tests); err != nil {
		t.Fatalf("parsing sqlite.yaml: %v", err)
	}
	return tests
}

func openTestDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite3", ":memory:")
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func execSQL(t *testing.T, db *sql.DB, sql string) {
	t.Helper()
	if _, err := db.Exec(sql); err != nil {
		t.Fatalf("Exec: %v\nSQL: %s", err, sql)
	}
}

func queryRows(t *testing.T, db *sql.DB, sqlStr string) []string {
	t.Helper()
	rows, err := db.Query(sqlStr)
	if err != nil {
		t.Fatalf("Query: %v\nSQL: %s", err, sqlStr)
	}
	defer rows.Close()
	cols, err := rows.Columns()
	if err != nil {
		t.Fatalf("Columns: %v", err)
	}
	var results []string
	for rows.Next() {
		// Scan all columns, return only the first (matches C++ behavior).
		vals := make([]sql.NullString, len(cols))
		ptrs := make([]interface{}, len(cols))
		for i := range vals {
			ptrs[i] = &vals[i]
		}
		if err := rows.Scan(ptrs...); err != nil {
			t.Fatalf("Scan: %v", err)
		}
		if vals[0].Valid {
			results = append(results, vals[0].String)
		} else {
			results = append(results, "NULL")
		}
	}
	if err := rows.Err(); err != nil {
		t.Fatalf("rows.Err: %v", err)
	}
	return results
}

func TestSQLiteIntegration(t *testing.T) {
	tests := loadSQLiteTests(t)
	for _, tc := range tests {
		t.Run(tc.Name, func(t *testing.T) {
			db := openTestDB(t)

			// Setup
			for _, sql := range tc.Setup {
				execSQL(t, db, sql)
			}

			var rows []string

			switch {
			case len(tc.Steps) > 0:
				// Multi-step (view recomposition etc.)
				for _, step := range tc.Steps {
					if step.TranspileExec != "" {
						sql, err := Transpile(step.TranspileExec)
						if err != nil {
							t.Fatalf("Transpile exec: %v", err)
						}
						execSQL(t, db, sql)
					}
					if step.TranspileQuery != "" {
						sql, err := Transpile(step.TranspileQuery)
						if err != nil {
							t.Fatalf("Transpile query: %v", err)
						}
						rows = queryRows(t, db, sql)
					}
				}

			case tc.RawSQL != "":
				rows = queryRows(t, db, tc.RawSQL)

			case len(tc.FKs) > 0:
				sql, err := TranspileFK(tc.Input, toFKs(tc.FKs))
				if err != nil {
					t.Fatalf("TranspileFK: %v", err)
				}
				rows = queryRows(t, db, sql)

			default:
				sql, err := Transpile(tc.Input)
				if err != nil {
					t.Fatalf("Transpile: %v", err)
				}
				rows = queryRows(t, db, sql)
			}

			// Verify
			if len(tc.ExpectedContains) > 0 {
				if len(rows) != 1 {
					t.Fatalf("expected 1 row, got %d", len(rows))
				}
				for _, needle := range tc.ExpectedContains {
					if !strings.Contains(rows[0], needle) {
						t.Errorf("missing %q in %q", needle, rows[0])
					}
				}
			} else {
				if len(rows) != len(tc.Expected) {
					t.Fatalf("row count: got %d, want %d", len(rows), len(tc.Expected))
				}
				for i, want := range tc.Expected {
					if rows[i] != want {
						t.Errorf("row[%d]:\n got = %q\nwant = %q", i, rows[i], want)
					}
				}
			}
		})
	}
}
