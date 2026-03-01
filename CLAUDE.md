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
   ArrayLiteral | JoinPath) is the spine. Represents SQL-inside-JSON-inside-SQL
   to arbitrary nesting depth. `DeepSelect` has a `bool singular` flag for
   `SELECT/1` (singular select).

3. **Alias pre-scan** — before parsing, a lightweight lexer pass builds a global
   `alias → table name` map from FROM/JOIN clauses. This allows join path
   resolution even when the alias definition appears later in the source
   (SQL defines aliases in FROM, which comes after the SELECT projection).
   Handles both `->` and `<-` arrows, and chains of arbitrary length.

4. **Parser** — unified recursive descent. Scans SQL tokens, descending into
   deep construct parsing when `SELECT {`, `SELECT [`, `{`, or `[` is
   encountered. `try_consume_singular()` detects the `/1` suffix after SELECT.
   Handles `(SELECT {/[)` subquery pattern specially to avoid double-wrapping
   parens. Tracks paren depth and string literals to distinguish structural
   commas from expression-internal commas. Detects
   `ident (-> | <-) table [alias] ...` join path patterns and emits `JoinPath`
   AST nodes supporting arbitrary chains.

5. **Renderer** — walks AST, emits standard SQL. Object literals become
   `json_object(...)`, array literals become `json_array(...)`, deep selects
   at top level emit `SELECT json_object/json_group_array(...)`, nested deep
   selects wrap in `(SELECT json_group_array(...))`. Singular selects
   (`SELECT/1`) skip `json_group_array` wrapping and append `LIMIT 1`.
   Join paths emit
   `FROM step1_table [JOIN step2 ON ...] WHERE step1.start_table_id = start.start_table_id`.

### Syntax

- `SELECT { fields } FROM ...` → `SELECT json_object(...) FROM ...`
- `FROM ... SELECT { fields }` → same output (FROM-first alternative)
- Nested `SELECT { ... } FROM ...` → `(SELECT json_group_array(json_object(...)) FROM ...)`
- Nested `FROM ... SELECT { ... }` → same output (FROM-first alternative)
- `SELECT [expr] FROM ...` → `SELECT json_group_array(expr) FROM ...`
- `FROM ... SELECT [expr]` → same output (FROM-first alternative)
- `SELECT/1 { fields } FROM ...` → `SELECT json_object(...) FROM ... LIMIT 1` (singular: one row, no array wrapping)
- `SELECT/1 [expr] FROM ...` → `SELECT expr FROM ... LIMIT 1` (singular array: single element unwrapped)
- `FROM ... SELECT/1 { fields }` → same output (FROM-first alternative)
- Nested `SELECT/1 { ... }` → `(SELECT json_object(...) FROM ... LIMIT 1)` (returns object or null)
- `FROM ... SELECT expr` → `SELECT expr FROM ...` (plain rearrangement, no JSON wrapping)
- `[expr, ...]` → `json_array(...)`
- `{ fields }` → `json_object(...)` (inline)
- Bare field: `id,` → `'id', id`
- Renamed: `order_id: id` → `'order_id', id`
- Double-quoted key: `"order id": id` → `'order id', id`
- Forward join: `FROM c->orders o` → `FROM orders o WHERE o.customers_id = c.customers_id`
  (`->` = right table is child, has FK `<left_table>_id`)
- Reverse join: `FROM o<-customers c` → `FROM customers c WHERE o.customers_id = c.customers_id`
  (`<-` = left table is child, has FK `<right_table>_id`)
- Chain: `FROM c->orders o->items i` → `FROM orders o JOIN items i ON i.orders_id = o.orders_id WHERE o.customers_id = c.customers_id`
- Bridge (many-to-many): `FROM c->custacct<-accounts a` → `FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id WHERE custacct.customers_id = c.customers_id`
- Convention: PK = `<table>_id`, FK = same column name in child/parent table
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
  depth limits, auto-join (`->`), reverse join (`<-`), join path chains
  (grandchild, bridge/many-to-many, three-step), FROM-first variants (deep and
  plain), singular select (`SELECT/1`) variants, error cases

- `test_sqlite.cpp` — integration tests: transpile sqldeep syntax, execute the
  resulting SQL against an in-memory SQLite database, and verify the JSON output.
  Covers single-table queries, two- and three-level nesting, mixed array/object
  nesting, empty subquery results, WHERE clauses, auto-join, reverse join,
  grandchild chain, bridge join (many-to-many), FROM-first variants, singular
  select (`SELECT/1`), and plain SQL passthrough.

Add new tests to `test_transpile.cpp` or create new `test_*.cpp` files for
focused component testing.

## Conventions

- Apache 2.0 license with SPDX headers on all source files.
- No logging — errors communicated via `sqldeep::Error` exceptions.
