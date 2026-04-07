// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Swift wrapper around the C transpiler API (dist/sqldeep.h).

import Foundation
import CSQLDeep

// MARK: - Error type

/// Error returned by the sqldeep transpiler.
public struct SQLDeepError: Error, CustomStringConvertible {
    public let message: String
    public let line: Int
    public let column: Int

    public var description: String {
        "line \(line), col \(column): \(message)"
    }
}

// MARK: - FK types for Swift callers

/// A single column mapping in a foreign key relationship.
public struct SQLDeepColumnPair {
    public let fromColumn: String
    public let toColumn: String

    public init(fromColumn: String, toColumn: String) {
        self.fromColumn = fromColumn
        self.toColumn = toColumn
    }
}

/// A foreign key relationship between two tables.
public struct SQLDeepForeignKey {
    public let fromTable: String
    public let toTable: String
    public let columns: [SQLDeepColumnPair]

    public init(fromTable: String, toTable: String, columns: [SQLDeepColumnPair]) {
        self.fromTable = fromTable
        self.toTable = toTable
        self.columns = columns
    }
}

// MARK: - Public API

/// Transpile sqldeep input to SQL (convention-based, SQLite backend).
public func sqldeepTranspile(_ input: String) throws -> String {
    var errMsg: UnsafeMutablePointer<CChar>?
    var errLine: Int32 = 0
    var errCol: Int32 = 0

    guard let result = sqldeep_transpile(input, &errMsg, &errLine, &errCol) else {
        let msg = errMsg.map { String(cString: $0) } ?? "unknown error"
        if let errMsg { sqldeep_free(errMsg) }
        throw SQLDeepError(message: msg, line: Int(errLine), column: Int(errCol))
    }
    defer { sqldeep_free(result) }
    return String(cString: result)
}

/// Transpile sqldeep input to SQL with explicit FK metadata (SQLite backend).
public func sqldeepTranspileFK(_ input: String, foreignKeys: [SQLDeepForeignKey]) throws -> String {
    var errMsg: UnsafeMutablePointer<CChar>?
    var errLine: Int32 = 0
    var errCol: Int32 = 0

    // Build C FK structs.
    var cPairs: [[sqldeep_column_pair]] = []
    var cFKs: [sqldeep_foreign_key] = []

    for fk in foreignKeys {
        let pairs = fk.columns.map { col in
            sqldeep_column_pair(
                from_column: UnsafePointer(strdup(col.fromColumn)),
                to_column: UnsafePointer(strdup(col.toColumn))
            )
        }
        cPairs.append(pairs)
    }

    // Build FK descriptors pointing into cPairs arrays.
    for (i, fk) in foreignKeys.enumerated() {
        let cfk = cPairs[i].withUnsafeBufferPointer { buf in
            sqldeep_foreign_key(
                from_table: UnsafePointer(strdup(fk.fromTable)),
                to_table: UnsafePointer(strdup(fk.toTable)),
                columns: buf.baseAddress,
                column_count: Int32(buf.count)
            )
        }
        cFKs.append(cfk)
    }

    defer {
        // Free strdup'd strings.
        for cfk in cFKs {
            free(UnsafeMutablePointer(mutating: cfk.from_table))
            free(UnsafeMutablePointer(mutating: cfk.to_table))
        }
        for pairs in cPairs {
            for p in pairs {
                free(UnsafeMutablePointer(mutating: p.from_column))
                free(UnsafeMutablePointer(mutating: p.to_column))
            }
        }
    }

    guard let result = cFKs.withUnsafeBufferPointer({ fkBuf in
        sqldeep_transpile_fk(input, fkBuf.baseAddress, Int32(fkBuf.count),
                             &errMsg, &errLine, &errCol)
    }) else {
        let msg = errMsg.map { String(cString: $0) } ?? "unknown error"
        if let errMsg { sqldeep_free(errMsg) }
        throw SQLDeepError(message: msg, line: Int(errLine), column: Int(errCol))
    }
    defer { sqldeep_free(result) }
    return String(cString: result)
}
