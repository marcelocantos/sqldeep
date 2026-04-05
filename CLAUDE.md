# sqldeep

Transpiler for JSON5-like SQL syntax to standard SQL with database-specific
JSON functions. Supports SQLite (default) and PostgreSQL backends.
Single public C header (`sqldeep.h`) with implementation in `sqldeep.cpp`.

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

Pure `string → string` transformation. No database dependency. The `Backend`
enum selects which JSON function names to emit (SQLite: `json_object`,
`json_array`, `json_group_array`; PostgreSQL: `jsonb_build_object`,
`jsonb_build_array`, `jsonb_agg`). All other SQL (JOIN, WHERE, LIMIT, etc.)
is standard and works unchanged across backends.

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
   `ident (-> | <-) table [alias] [ON/USING] ...` join path patterns and emits
   `JoinPath` AST nodes supporting arbitrary chains with optional explicit
   column specifications. Detects `(expr).path` JSON path patterns during
   token accumulation using a paren-position stack, transforming them inline
   to `json_extract()`/`jsonb_extract_path()` based on backend. Detects
   `<ident` as XML element start (unambiguous — `<` cannot start a SQL
   expression) and dispatches to `parse_xml_element()` which uses
   `read_raw_until_xml_special()` for body text between tags.

5. **Renderer** — walks AST, emits standard SQL. Parameterised by `Backend`:
   JSON function names (`fn_object_`, `fn_array_`, `fn_group_array_`) are set
   at construction. Object literals become `fn_object_(...)`, array literals
   become `fn_array_(...)`, deep selects wrap in `fn_group_array_(...)`.
   Singular selects (`SELECT/1`) skip group-array wrapping and append
   `LIMIT 1`.
   Join paths emit
   `FROM step1_table [JOIN step2 ON ...] WHERE step1.start_table_id = start.start_table_id`.
   Column names come from inline ON/USING clauses when present, then
   explicit FK metadata when provided, or the `<table>_id` convention
   otherwise. Multi-column joins produce AND-joined conditions.

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
- `field: SELECT expr` (inside `{ }`, no FROM) → `'field', json_group_array(expr)` (aggregate over current GROUP BY scope)
- `field: SELECT/1 expr` (inside `{ }`, no FROM) → `'field', expr` (singular: no array wrapping)
- `[expr, ...]` → `json_array(...)`
- `{ fields }` → `json_object(...)` (inline)
- Bare field: `id,` → `'id', id`
- Renamed: `order_id: id` → `'order_id', id`
- Double-quoted key: `"order id": id` → `'order id', id`
- Computed key: `(expr): val` → `expr, val` (key is a runtime expression)
- Forward join: `FROM c->orders o` → `FROM orders o WHERE o.customers_id = c.customers_id`
  (`->` = right table is child, has FK `<left_table>_id`)
- Reverse join: `FROM o<-customers c` → `FROM customers c WHERE o.customers_id = c.customers_id`
  (`<-` = left table is child, has FK `<right_table>_id`)
- Chain: `FROM c->orders o->items i` → `FROM orders o JOIN items i ON i.orders_id = o.orders_id WHERE o.customers_id = c.customers_id`
- Bridge (many-to-many): `FROM c->custacct<-accounts a` → `FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id WHERE custacct.customers_id = c.customers_id`
- ON shorthand: `c->orders ON person_id` → `orders.person_id = c.person_id`
  (same column name in both tables)
- ON explicit: `c->orders o ON id = cust_id` → `o.cust_id = c.id`
  (left_col from left of arrow, right_col from right of arrow)
- ON multi-column: `c->orders o ON id = cust_id AND region = region`
  → `o.cust_id = c.id AND o.region = c.region`
- USING: `c->orders o USING (person_id)` → `o.person_id = c.person_id`
  (same column name(s) in both tables, standard SQL style)
- ON/USING in chains: `c->orders o ON id = cust_id->items i ON oid = order_ref`
  (each step can have its own ON/USING; steps without fall back to convention/FK)
- JSON path: `(expr).field.sub[n]` → `json_extract(expr, '$.field.sub[n]')` (SQLite)
  / `jsonb_extract_path(expr, 'field', 'sub', 'n')` (PostgreSQL).
  Parentheses around the base expression disambiguate from `table.column`.
  Works at any paren depth (e.g. `upper((data).name)`).
- Convention: PK = `<table>_id`, FK = same column name in child/parent table
  (default when no ON/USING and no FK metadata provided)
- FK-guided mode: `transpile(input, foreign_keys)` uses explicit FK metadata
  instead of the naming convention. Supports multi-column FKs. Errors on
  missing or ambiguous FK matches (no convention fallback).
- Recursive tree: `SELECT/1 { id, name, children: * } FROM t RECURSE ON (parent_id) WHERE parent_id IS NULL`
  → 3-CTE bracket-injection template producing nested JSON entirely within SQL
- `RECURSE ON (fk = pk)` for explicit PK column (default: `id`)
- `SELECT { ..., children: * } FROM t RECURSE ON (fk)` → forest output wrapped in `[]`
- XML element: `<div class="card">{name}</div>` → `xml_element('div', xml_attrs('class', 'card'), name)`
- XML self-closing: `<br/>` → `xml_element('br')`
- XML interpolation: `{expr}` inside XML content or attributes
- XML subquery: `{SELECT <li>{name}</li> FROM t}` inside XML
  → `(SELECT xml_agg(xml_element('li', name)) FROM t)`
- XML singular subquery: `{SELECT/1 <span>{name}</span> FROM t}`
  → `(SELECT xml_element('span', name) FROM t LIMIT 1)`
- XML namespaced tags: `<ui:Table.Cell>` → `xml_element('ui:Table.Cell', ...)`
- XML boolean attribute: `<input disabled/>` → `xml_element('input', xml_attrs('disabled', 1))`
- XML inside JSON: `{ card: <div>{name}</div> }` → `json_object('card', xml_element('div', name))`
- JSON object inside XML: `<td>{{name, qty}}</td>` → `xml_element('td', json_object('name', name, 'qty', qty))`
- JSON path inside XML: `<td>{(data).field}</td>` → `xml_element('td', json_extract(data, '$.field'))`
- Literal brace in XML: `{'{'}` → `'{'`
- `--` line comments stripped
- `/* ... */` block comments stripped (flat, not nested)
- Trailing commas allowed

## File layout

```
dist/
  sqldeep.h                 Public C header (FFI for cgo, etc.)
  sqldeep.cpp               Implementation (C++ internals, lexer, AST, parser, renderer, C bridge)
  sqldeep-agents-guide.md   Agent integration guide
tests/                      doctest test files
examples/                   demo.cpp
vendor/                     Third-party dependencies
mkfile                      Build system (mk)
```

## Tests

- `test_transpile.cpp` — end-to-end transpilation for both SQLite and PostgreSQL
  backends: basic objects, renamed fields, trailing commas, inline arrays,
  nested subqueries (2- and 3-level), mixed array/object nesting, comments, SQL
  passthrough, key escaping, nesting depth limits, auto-join (`->`), reverse
  join (`<-`), join path chains (grandchild, bridge/many-to-many, three-step),
  ON/USING explicit column clauses (shorthand, explicit pairs, multi-column,
  reverse, chains, bridge, FROM-first, mixed), JSON path extraction
  (`(expr).field[n]` — simple, nested, qualified, array index, mixed, function
  base, WHERE clause, inside function, array-only, passthrough, error cases),
  FROM-first variants (deep and plain), singular select (`SELECT/1`) variants,
  FK-guided joins (forward, reverse, chain, bridge, multi-column, error cases),
  recursive select (`RECURSE ON`) variants (basic tree, forest, explicit PK,
  PostgreSQL backend), XML literals (static elements, attributes, interpolation,
  nested elements, subqueries, namespaced tags, self-closing, boolean attributes,
  XML inside JSON, JSON object inside XML, JSON path inside XML, literal braces,
  error cases)

- `test_sqlite.cpp` — integration tests: transpile sqldeep syntax, execute the
  resulting SQL against an in-memory SQLite database, and verify the JSON output.
  Covers single-table queries, two- and three-level nesting, mixed array/object
  nesting, empty subquery results, WHERE clauses, auto-join, reverse join,
  grandchild chain, bridge join (many-to-many), FROM-first variants, singular
  select (`SELECT/1`), FK-guided joins (single and multi-column), JSON arrow
  operators (`->` / `->>`), recursive tree construction (singular, forest,
  empty), XML literals (static, dynamic attributes, interpolation, escaping,
  nested, self-closing, boolean attrs, subquery aggregation, XML inside JSON,
  empty subquery), and plain SQL passthrough.

Add new tests to `test_transpile.cpp` or create new `test_*.cpp` files for
focused component testing.

## Versioning and stability

- Semver (vMAJOR.MINOR.PATCH). Breaking changes require a new major version.
- Stability is tracked **per feature** in `STABILITY.md`, not per release.
- New features land as **Experimental** — usable but not yet covered by the
  backwards-compatibility contract. May change in any minor release.
- Features are promoted to **Stable** after real-world usage confirms the
  design. Promotion is a one-way door.
- Stable items are never affected by the addition of Experimental features.

## Delivery

Merged to master.

## Conventions

- Apache 2.0 license with SPDX headers on all source files.
- No logging — errors communicated via NULL return + error out-params in the
  C API (internally via `sqldeep::Error` exceptions).
