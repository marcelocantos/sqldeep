// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

// Package sqldeep transpiles JSON5-like SQL syntax to standard SQL with
// database-specific JSON functions (SQLite or PostgreSQL).
//
// The core function is [Transpile], which takes sqldeep syntax and returns
// standard SQL with SQLite JSON functions. For PostgreSQL output, use
// [TranspilePostgres] or the general [TranspileBackend].
//
// For schemas with explicit foreign key metadata, use [TranspileFK] (or
// [TranspileFKPostgres] / [TranspileFKBackend]) to resolve join paths from
// provided FK definitions instead of the default naming convention.
//
// This package wraps the C++ sqldeep library via cgo. It requires the
// static library (build/libsqldeep.a) to be built before `go build`.
package sqldeep
