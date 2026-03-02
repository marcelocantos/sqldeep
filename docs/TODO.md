# TODO

## Syntax enhancements

- [ ] **Allow ON and USING clauses after `a->b` syntax**
  Override the default FK convention when the column name doesn't match
  `<parent_table>_id`. E.g., `c->orders ON person_id` or
  `c->orders USING (cust_id)`.

## Portability

- [ ] **Port C++ implementation to C**
  A pure C implementation would dramatically widen sqldeep's reach — C has
  stable ABI, trivial FFI from virtually every language (Go via cgo, Python
  via ctypes/cffi, Rust via bindgen, etc.), and could be vendored alongside
  sqlite3.c as a companion `.c`/`.h` pair. Considerations:
  - **API**: `sqldeep_transpile(const char* input, char** output, char** error)`
    with caller-frees or a `sqldeep_free()` function. FK-guided variant takes
    an array of FK structs.
  - **Parser**: Rewrite the recursive descent parser in C. The lexer and parser
    are already straightforward — the main challenge is replacing `std::string`,
    `std::vector`, `std::variant`, and `std::unique_ptr` with manual buffer
    management.
  - **Coexistence**: Could maintain both C and C++ versions, or make C++ a thin
    wrapper over the C core. The latter avoids divergence.
  - **SQLite loadable extension**: A C implementation could be compiled as a
    SQLite loadable extension (`sqlite3_extension_init`), adding a
    `sqldeep_transpile()` SQL function directly inside SQLite.

## Integration

- [ ] **Transparent SQLite integration — investigate approaches for C++ and Go**
  Explore ways to make sqldeep transpilation seamless so that any SQL sent to
  SQLite is automatically preprocessed. Approaches to consider:
  - **C++**: Thin wrapper class around `sqlite3*` that intercepts
    `prepare`/`exec` calls and runs `sqldeep::transpile()` first. Could also
    explore SQLite's `sqlite3_set_authorizer` or virtual table hooks, though
    these don't intercept SQL text directly. A `sqldeep::DB` RAII wrapper that
    owns the connection and transparently transpiles would be the most
    ergonomic.
  - **Go**: Implement a `database/sql/driver.Driver` that wraps an underlying
    SQLite driver (e.g., `modernc.org/sqlite` or `mattn/go-sqlite3`),
    transpiling SQL strings in `Prepare()` and `Exec()` before passing them
    through. Alternatively, a middleware approach at the `*sql.DB` level using
    a custom `ConnectorFunc`. A third option is a `go-sqlite3` extension
    loaded via `sql.Register` that applies transpilation.
  - **Shared concern**: Caching — transpilation results should be cached by
    input hash to avoid re-transpiling the same query. Passthrough detection
    (no `{`, `[`, `->`, `<-` tokens) could skip transpilation entirely for
    plain SQL.
