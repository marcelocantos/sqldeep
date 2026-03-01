# sqldeep

Transpiler for JSON5-like SQL syntax to standard SQLite JSON functions.
Two-file library: `sqldeep.h` (public API) and `sqldeep.cpp` (implementation).

## Build

```sh
mk test     # build and run all tests
mk example  # build and run examples/demo.cpp
mk clean    # remove build/
```

Requires C++20. Uses [mk](https://github.com/marcelocantos/mk) as the build
system (`mkfile`).

## Dependencies

- **doctest** — vendored in `vendor/include/doctest.h` (test only)
- **SQLite 3** — vendored in `vendor/include/sqlite3.h` + `vendor/src/sqlite3.c`
  (integration tests only; the library itself has no SQLite dependency)

## Architecture

Pure `string → string` transformation. No SQLite dependency.

### Components (all internal to `sqldeep.cpp`)

1. **Lexer** — tokenizes input with source position tracking (line/col/offset).
   Strips `//` comments. Handles single-quoted strings (SQL), double-quoted
   strings (JSON-style keys), identifiers, numbers, operators.

2. **AST** — `SqlParts` (vector of string | DeepSelect | ObjectLiteral |
   ArrayLiteral | AutoJoin) is the spine. Represents SQL-inside-JSON-inside-SQL
   to arbitrary nesting depth.

3. **Alias pre-scan** — before parsing, a lightweight lexer pass builds a global
   `alias → table name` map from FROM/JOIN clauses. This allows `->` auto-join
   resolution even when the alias definition appears later in the source
   (SQL defines aliases in FROM, which comes after the SELECT projection).

4. **Parser** — unified recursive descent. Scans SQL tokens, descending into
   deep construct parsing when `SELECT {`, `SELECT [`, `{`, or `[` is
   encountered. Handles `(SELECT {/[)` subquery pattern specially to avoid
   double-wrapping parens. Tracks paren depth and string literals to
   distinguish structural commas from expression-internal commas. Detects
   `ident->ident` auto-join patterns and emits `AutoJoin` AST nodes.

5. **Renderer** — walks AST, emits standard SQL. Object literals become
   `json_object(...)`, array literals become `json_array(...)`, deep selects
   at top level emit `SELECT json_object/json_group_array(...)`, nested deep
   selects wrap in `(SELECT json_group_array(...))`, auto-joins emit
   `FROM child WHERE child.parent_id = alias.parent_id`.

### Syntax

- `SELECT { fields } FROM ...` → `SELECT json_object(...) FROM ...`
- `FROM ... SELECT { fields }` → same output (FROM-first alternative)
- Nested `SELECT { ... } FROM ...` → `(SELECT json_group_array(json_object(...)) FROM ...)`
- Nested `FROM ... SELECT { ... }` → same output (FROM-first alternative)
- `SELECT [expr] FROM ...` → `SELECT json_group_array(expr) FROM ...`
- `FROM ... SELECT [expr]` → same output (FROM-first alternative)
- `[expr, ...]` → `json_array(...)`
- `{ fields }` → `json_object(...)` (inline)
- Bare field: `id,` → `'id', id`
- Renamed: `order_id: id` → `'order_id', id`
- Double-quoted key: `"order id": id` → `'order id', id`
- Auto-join: `FROM c->orders o` → `FROM orders o WHERE o.customers_id = c.customers_id`
  (convention: PK = `<table>_id`, FK = same column name in child table)
- `//` line comments stripped
- Trailing commas allowed

## File layout

```
sqldeep.h           Public header (Error class, transpile())
sqldeep.cpp         Implementation (lexer, AST, parser, renderer)
tests/              doctest test files
examples/           demo.cpp
vendor/             Third-party dependencies
mkfile              Build system (mk)
```

## Tests

- `test_transpile.cpp` — end-to-end transpilation: basic objects, renamed
  fields, trailing commas, inline arrays, nested subqueries (2- and 3-level),
  mixed array/object nesting, comments, SQL passthrough, key escaping, nesting
  depth limits, error cases

- `test_sqlite.cpp` — integration tests: transpile sqldeep syntax, execute the
  resulting SQL against an in-memory SQLite database, and verify the JSON output.
  Covers single-table queries, two- and three-level nesting, mixed array/object
  nesting, empty subquery results, WHERE clauses, and plain SQL passthrough.

Add new tests to `test_transpile.cpp` or create new `test_*.cpp` files for
focused component testing.

## Conventions

- Apache 2.0 license with SPDX headers on all source files.
- No logging — errors communicated via `sqldeep::Error` exceptions.
