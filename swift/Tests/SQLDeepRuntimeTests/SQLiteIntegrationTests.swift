// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Data-driven SQLite integration tests for the Swift runtime.

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
        if let text = sqlite3_column_text(stmt, 0) {
            rows.append(String(cString: text))
        } else {
            rows.append("NULL")
        }
    }
    return rows
}

// MARK: - Transpile helper (calls the C API)
// The Swift runtime doesn't include the transpiler — it only has the
// SQLite functions. For now, skip tests that require transpilation
// (they test the transpiler + runtime together). The Swift tests focus
// on the runtime functions via raw_sql tests.
//
// TODO: Add C transpiler binding to Swift package, or extract runtime-only
// tests into a separate YAML section.

// MARK: - Tests

final class SQLiteIntegrationTests: XCTestCase {
    func testRuntimeFunctions() throws {
        // Load test data
        // Load from symlinked file in the test directory.
        let testDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        let url = testDir.appendingPathComponent("sqlite.yaml")
        let yamlStr = try String(contentsOf: url, encoding: .utf8)
        let tests = try YAMLDecoder().decode([SQLiteTestCase].self, from: yamlStr)

        let db = try openDB()
        defer { sqlite3_close(db) }

        // Run only raw_sql tests (these test the runtime functions directly,
        // without needing the transpiler). This validates the Swift port
        // produces identical output to the C implementation.
        let rawTests = tests.filter { $0.raw_sql != nil }
        XCTAssertGreaterThan(rawTests.count, 0, "no raw_sql tests found")

        for tc in rawTests {
            // Setup
            for sql in tc.setup ?? [] {
                try execSQL(db, sql)
            }

            let rows = try queryRows(db, tc.raw_sql!)

            if let expected = tc.expected {
                XCTAssertEqual(rows.count, expected.count,
                               "\(tc.name): row count mismatch")
                for (i, want) in expected.enumerated() {
                    XCTAssertEqual(rows[i], want,
                                   "\(tc.name) row[\(i)]")
                }
            }
        }
    }
}
