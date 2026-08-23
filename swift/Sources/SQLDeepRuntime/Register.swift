// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Thin Swift wrapper around dist/sqldeep_xml.c (via CSQLDeepXML).
// The SQLite runtime is the shared C implementation — same as Go/cgo and
// Kotlin/JNI — so NULL/escaping/BLOB-protocol behaviour cannot drift.

import CSQLDeepXML

/// Register all sqldeep runtime functions on a SQLite connection.
/// Returns SQLITE_OK on success.
@discardableResult
public func sqldeepRegisterSQLite(_ db: OpaquePointer) -> Int32 {
    sqldeep_register_sqlite(db)
}
