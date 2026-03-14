// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

package sqldeep

//#include "sqldeep.h"
//#include <stdlib.h>
import "C"
import "unsafe"

// Backend selects the target database for SQL generation.
type Backend int

const (
	SQLite   Backend = iota // SQLite JSON functions (default)
	Postgres                // PostgreSQL jsonb functions
)

// Error represents a transpilation error with source position.
type Error struct {
	Msg  string
	Line int
	Col  int
}

func (e *Error) Error() string { return e.Msg }

// ForeignKey describes a foreign key relationship from a child table to a
// parent table.
type ForeignKey struct {
	FromTable string
	ToTable   string
	Columns   []ColumnPair
}

// ColumnPair maps a foreign key column in the child table to the referenced
// column in the parent table.
type ColumnPair struct {
	FromColumn string
	ToColumn   string
}

// Transpile converts sqldeep syntax to standard SQL using the <table>_id
// naming convention for join path resolution.
func Transpile(input string) (string, error) {
	cinput := C.CString(input)
	defer C.free(unsafe.Pointer(cinput))

	var errMsg *C.char
	var errLine, errCol C.int
	result := C.sqldeep_transpile(cinput, &errMsg, &errLine, &errCol)
	if result == nil {
		return "", goError(errMsg, errLine, errCol)
	}
	defer C.sqldeep_free(unsafe.Pointer(result))
	return C.GoString(result), nil
}

// TranspileBackend converts sqldeep syntax to standard SQL for the specified
// backend, using the <table>_id naming convention for join path resolution.
func TranspileBackend(input string, backend Backend) (string, error) {
	cinput := C.CString(input)
	defer C.free(unsafe.Pointer(cinput))

	var errMsg *C.char
	var errLine, errCol C.int
	result := C.sqldeep_transpile_backend(cinput, C.sqldeep_backend(backend),
		&errMsg, &errLine, &errCol)
	if result == nil {
		return "", goError(errMsg, errLine, errCol)
	}
	defer C.sqldeep_free(unsafe.Pointer(result))
	return C.GoString(result), nil
}

// TranspilePostgres is a convenience wrapper for TranspileBackend with Postgres.
func TranspilePostgres(input string) (string, error) {
	return TranspileBackend(input, Postgres)
}

// TranspileFK converts sqldeep syntax to standard SQL using explicit foreign
// key metadata for join path resolution. No convention fallback is used.
func TranspileFK(input string, fks []ForeignKey) (string, error) {
	return TranspileFKBackend(input, fks, SQLite)
}

// TranspileFKBackend converts sqldeep syntax to standard SQL for the specified
// backend, using explicit foreign key metadata for join path resolution.
func TranspileFKBackend(input string, fks []ForeignKey, backend Backend) (string, error) {
	cinput := C.CString(input)
	defer C.free(unsafe.Pointer(cinput))

	// Build C struct arrays. Column pair arrays are C-allocated to satisfy
	// cgo pointer rules (Go pointers must not contain other Go pointers
	// when passed to C).
	cfks := make([]C.sqldeep_foreign_key, len(fks))
	// Track C-allocated column pair arrays for cleanup.
	colPtrs := make([]unsafe.Pointer, len(fks))
	defer func() {
		for _, p := range colPtrs {
			if p != nil {
				C.free(p)
			}
		}
	}()

	for i, fk := range fks {
		cfks[i].from_table = C.CString(fk.FromTable)
		defer C.free(unsafe.Pointer(cfks[i].from_table))
		cfks[i].to_table = C.CString(fk.ToTable)
		defer C.free(unsafe.Pointer(cfks[i].to_table))

		n := len(fk.Columns)
		cfks[i].column_count = C.int(n)
		if n > 0 {
			// Allocate C memory for the column pairs array.
			sz := C.size_t(n) * C.size_t(unsafe.Sizeof(C.sqldeep_column_pair{}))
			colPtrs[i] = C.malloc(sz)
			pairs := unsafe.Slice((*C.sqldeep_column_pair)(colPtrs[i]), n)
			for j, cp := range fk.Columns {
				pairs[j].from_column = C.CString(cp.FromColumn)
				defer C.free(unsafe.Pointer(pairs[j].from_column))
				pairs[j].to_column = C.CString(cp.ToColumn)
				defer C.free(unsafe.Pointer(pairs[j].to_column))
			}
			cfks[i].columns = (*C.sqldeep_column_pair)(colPtrs[i])
		}
	}

	var errMsg *C.char
	var errLine, errCol C.int

	var cfksPtr *C.sqldeep_foreign_key
	if len(cfks) > 0 {
		cfksPtr = &cfks[0]
	}
	result := C.sqldeep_transpile_fk_backend(cinput, C.sqldeep_backend(backend),
		cfksPtr, C.int(len(cfks)),
		&errMsg, &errLine, &errCol)
	if result == nil {
		return "", goError(errMsg, errLine, errCol)
	}
	defer C.sqldeep_free(unsafe.Pointer(result))
	return C.GoString(result), nil
}

// TranspileFKPostgres is a convenience wrapper for TranspileFKBackend with Postgres.
func TranspileFKPostgres(input string, fks []ForeignKey) (string, error) {
	return TranspileFKBackend(input, fks, Postgres)
}

// Version returns the sqldeep library version string.
func Version() string {
	return C.GoString(C.sqldeep_version())
}
