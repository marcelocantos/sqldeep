// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Data-driven SQLite integration tests for the Swift runtime + transpiler.

import XCTest
import SQLDeepRuntime
import Yams
#if canImport(SQLite3)
import SQLite3
#endif

// MARK: - YAML schema

struct SQLiteTestCase: Decodable {
    let name: String
    let setup: [String]?
    let input: String?
    let raw_sql: String?
    let expected: [String]?
    let expected_contains: [String]?
    let fks: [FKDef]?
    let steps: [StepDef]?
}

struct FKDef: Decodable {
    let from_table: String
    let to_table: String
    let columns: [ColPair]
}

struct ColPair: Decodable {
    let from_column: String
    let to_column: String
}

struct StepDef: Decodable {
    let transpile_exec: String?
    let transpile_query: String?
}

// MARK: - SQLite helpers

func openDB() throws -> OpaquePointer {
    var db: OpaquePointer?
    let rc = sqlite3_open(":memory:", &db)
    guard rc == SQLITE_OK, let db else {
        throw NSError(domain: "SQLite", code: Int(rc))
    }
    sqldeepRegisterSQLite(db)
    return db
}

func execSQL(_ db: OpaquePointer, _ sql: String) throws {
    var err: UnsafeMutablePointer<CChar>?
    let rc = sqlite3_exec(db, sql, nil, nil, &err)
    if rc != SQLITE_OK {
        let msg = err.map { String(cString: $0) } ?? "unknown"
        sqlite3_free(err)
        throw NSError(domain: "SQLite", code: Int(rc),
                      userInfo: [NSLocalizedDescriptionKey: msg])
    }
}

func queryRows(_ db: OpaquePointer, _ sql: String) throws -> [String] {
    var stmt: OpaquePointer?
    guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
        throw NSError(domain: "SQLite", code: -1,
                      userInfo: [NSLocalizedDescriptionKey: String(cString: sqlite3_errmsg(db))])
    }
    defer { sqlite3_finalize(stmt) }

    var rows: [String] = []
    while sqlite3_step(stmt) == SQLITE_ROW {
        if sqlite3_column_type(stmt, 0) == SQLITE_BLOB {
            let n = sqlite3_column_bytes(stmt, 0)
            if n > 0, let p = sqlite3_column_blob(stmt, 0) {
                rows.append(String(data: Data(bytes: p, count: Int(n)), encoding: .utf8) ?? "")
            } else {
                rows.append("")
            }
        } else if let text = sqlite3_column_text(stmt, 0) {
            rows.append(String(cString: text))
        } else {
            rows.append("NULL")
        }
    }
    return rows
}

// MARK: - Transpile + execute helpers

func transpile(_ input: String, fks: [FKDef]?) throws -> String {
    if let fks, !fks.isEmpty {
        let swiftFKs = fks.map { fk in
            SQLDeepForeignKey(
                fromTable: fk.from_table,
                toTable: fk.to_table,
                columns: fk.columns.map {
                    SQLDeepColumnPair(fromColumn: $0.from_column, toColumn: $0.to_column)
                }
            )
        }
        return try sqldeepTranspileFK(input, foreignKeys: swiftFKs)
    }
    return try sqldeepTranspile(input)
}

// MARK: - Tests

final class SQLiteIntegrationTests: XCTestCase {
    func testAllCases() throws {
        // Load test data from symlinked file in the test directory.
        let testDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        let url = testDir.appendingPathComponent("sqlite.yaml")
        let yamlStr = try String(contentsOf: url, encoding: .utf8)
        let tests = try YAMLDecoder().decode([SQLiteTestCase].self, from: yamlStr)

        XCTAssertEqual(tests.count, 79, "expected 79 test cases in sqlite.yaml")

        for tc in tests {
            let db = try openDB()
            defer { sqlite3_close(db) }

            // Setup
            for sql in tc.setup ?? [] {
                do {
                    try execSQL(db, sql)
                } catch {
                    XCTFail("\(tc.name): setup failed: \(error)")
                    continue
                }
            }

            // Determine the query SQL and execute.
            var rows: [String]

            if let rawSQL = tc.raw_sql {
                // raw_sql: execute directly, no transpilation.
                rows = try queryRows(db, rawSQL)
            } else if let steps = tc.steps {
                // Multi-step test: transpile and execute each step.
                rows = []
                for step in steps {
                    if let execInput = step.transpile_exec {
                        let sql = try transpile(execInput, fks: tc.fks)
                        try execSQL(db, sql)
                    } else if let queryInput = step.transpile_query {
                        let sql = try transpile(queryInput, fks: tc.fks)
                        rows = try queryRows(db, sql)
                    }
                }
            } else if let input = tc.input {
                // Standard: transpile input and query.
                let sql = try transpile(input, fks: tc.fks)
                rows = try queryRows(db, sql)
            } else {
                XCTFail("\(tc.name): no input, raw_sql, or steps")
                continue
            }

            // Verify results.
            if let expected = tc.expected {
                XCTAssertEqual(rows.count, expected.count,
                               "\(tc.name): row count mismatch (got \(rows.count), want \(expected.count))")
                for (i, want) in expected.enumerated() {
                    guard i < rows.count else { break }
                    XCTAssertEqual(rows[i], want,
                                   "\(tc.name) row[\(i)]")
                }
            }

            if let contains = tc.expected_contains {
                XCTAssertEqual(rows.count, 1,
                               "\(tc.name): expected_contains requires exactly 1 row, got \(rows.count)")
                if let row = rows.first {
                    for sub in contains {
                        XCTAssertTrue(row.contains(sub),
                                      "\(tc.name): expected row to contain \"\(sub)\", got \"\(row)\"")
                    }
                }
            }
        }
    }
}
