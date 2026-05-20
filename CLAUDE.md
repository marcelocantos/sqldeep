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

Pure `string → string` transformation. No database dependency at parse/
transform time. The `Backend` enum selects which JSON function names to
emit (SQLite: `sqldeep_json_object`, `sqldeep_json_array`,
`sqldeep_json_group_array`; PostgreSQL: `jsonb_build_object`,
`jsonb_build_array`, `jsonb_agg`; SQLite_vanilla: built-in `json_object`
/ `json_array` / `json_group_array`). All other SQL (JOIN, WHERE, LIMIT,
etc.) is standard and works unchanged across backends.

### Pipeline

Sqldeep is implemented as an AST-rewrite layer on top of
[deepparser](https://github.com/marcelocantos/deepparser), a fork of
sqliteai/liteparser with sqldeep grammar extensions. The pipeline:

```
SQL text  ─▶  lp_parse_all              (deepparser — real SQL parser)
          ─▶  Transformer::transform    (sqldeep — mutates LpNode in place)
          ─▶  lp_ast_to_sql             (deepparser — canonical unparser)
          ─▶  output SQL
```

Deepparser owns parsing and canonical printing. Sqldeep owns the
dialect-specific transformation: a single `Transformer` class that
walks the AST post-order and rewrites every sqldeep extension node
(`LP_EXPR_SQLDEEP_OBJECT/ARRAY/FIELD/JOIN_PATH/JSON_PATH/XML/RECURSE`,
the `sqldeep_singular`/`sqldeep_from_first` flags on `LP_STMT_SELECT`)
into standard SQL node kinds (`LP_EXPR_FUNCTION`, `LP_FROM_TABLE`/
`LP_JOIN_CLAUSE`, `LP_WITH`+`LP_CTE`, `LP_LIMIT`). Once the AST contains
only plain SQL kinds, deepparser's canonical unparser emits the result.

Bare-expression inputs (`<div/>`, `{a, b}`) are wrapped in `SELECT …`
for parsing and unwrapped on output, preserving sqldeep's historical
"give me a fragment" calling convention.

### Components (all in `dist/sqldeep.cpp`)

- **`Transformer`** — the AST walker. One member function per sqldeep
  node kind (`rewrite_object`, `rewrite_array`, `rewrite_json_path`,
  `rewrite_xml`, `rewrite_join_path`, `rewrite_recursive_select`,
  `rewrite_select`), plus AST-building helpers (`make_function`,
  `make_column_ref2`, `make_join`, `make_binop`, etc.). Recursion is
  post-order so children are already standard SQL by the time their
  parent's rewriter runs.

- **Alias map** — one prepass over the AST collects every `alias → table_name` pair from FROM clauses and join-path steps so a join arrow
  like `c->orders` can resolve the leftmost name to the underlying
  table even when the alias is introduced by an enclosing SELECT.

- **FK index** — a `(child_table, parent_table) → ColumnPair[]` map
  built from the user-supplied `sqldeep_foreign_key` array. Used by
  the join-path rewriter when in FK-guided mode; unused in convention
  mode (`<parent>_id` everywhere).

- **C API bridge** — the `extern "C"` shim that wraps the Transformer
  pipeline behind `sqldeep_transpile_*` and converts the C
  `sqldeep_foreign_key` structs into the C++ `ForeignKey` vector.

The recursive SELECT (`RECURSE ON (fk [= pk])`) is rewritten by
constructing its 3-CTE bracket-injection template as SQL text and
re-parsing that text through deepparser to produce a standard SQL
AST. The shape (`_sdq_dfs` DFS + `_sdq_ranked` row-numbered objects
+ `_sdq_events` open/close fragments stitched by `group_concat` /
`string_agg`) is identical to the old hand-written renderer.

### Deepparser fork

The `vendor/deepparser/` submodule tracks the `sqldeep-grammar` branch
of `marcelocantos/deepparser`. The deepparser delta against upstream
liteparser is intentionally minimal — grammar extensions and canonical
unparser support for the sqldeep AST nodes, plus a handful of
canonical-formatting tweaks (no `AS` on aliases, tight `->`/`->>`
spacing, unquoted `true`/`false`, bare `JOIN` over `INNER JOIN`, no
synthesised default window frame). All dialect / transformation
knowledge lives in sqldeep, not in deepparser.

### Syntax

- `SELECT { fields } FROM ...` → `SELECT sqldeep_json_object(...) FROM ...`
- `FROM ... SELECT { fields }` → same output (FROM-first alternative)
- Nested `SELECT { ... } FROM ...` → `(SELECT sqldeep_json_group_array(sqldeep_json_object(...)) FROM ...)`
- Nested `FROM ... SELECT { ... }` → same output (FROM-first alternative)
- `SELECT [expr] FROM ...` → `SELECT sqldeep_json_group_array(expr) FROM ...`
- `FROM ... SELECT [expr]` → same output (FROM-first alternative)
- `SELECT/1 { fields } FROM ...` → `SELECT sqldeep_json_object(...) FROM ... LIMIT 1` (singular: one row, no array wrapping)
- `SELECT/1 [expr] FROM ...` → `SELECT expr FROM ... LIMIT 1` (singular array: single element unwrapped)
- `FROM ... SELECT/1 { fields }` → same output (FROM-first alternative)
- Nested `SELECT/1 { ... }` → `(SELECT sqldeep_json_object(...) FROM ... LIMIT 1)` (returns object or null)
- `FROM ... SELECT expr` → `SELECT expr FROM ...` (plain rearrangement, no JSON wrapping)
- `field: SELECT expr` (inside `{ }`, no FROM) → `'field', sqldeep_json_group_array(expr)` (aggregate over current GROUP BY scope)
- `field: SELECT/1 expr` (inside `{ }`, no FROM) → `'field', expr` (singular: no array wrapping)
- `[expr, ...]` → `sqldeep_json_array(...)`
- `{ fields }` → `sqldeep_json_object(...)` (inline)
- Bare field: `id,` → `'id', id`
- Qualified bare field: `sm.repo,` → `'repo', sm.repo` (key is last component)
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
- JSON path: `(expr).field.sub[n]` → `json_extract(CAST((expr) AS TEXT), '$.field.sub[n]')` (SQLite)
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
- XML self-closing (void): `<br/>` → `xml_element('br/')` (trailing `/` signals void element → `<br/>`)
- XML empty non-void: `<div></div>` → `xml_element('div')` (renders `<div></div>`, not `<div/>`)
- XML multi-line dedent: common leading whitespace prefix is stripped across
  lines so source indentation produces relative indentation in output
- XML interpolation: `{expr}` inside XML content or attributes
- XML subquery: `{SELECT <li>{name}</li> FROM t}` inside XML
  → `(SELECT xml_agg(xml_element('li', name)) FROM t)`
- XML singular subquery: `{SELECT/1 <span>{name}</span> FROM t}`
  → `(SELECT xml_element('span', name) FROM t LIMIT 1)`
- XML namespaced tags: `<ui:Table.Cell>` → `xml_element('ui:Table.Cell', ...)`
- XML boolean attribute: `<input disabled/>` → `xml_element('input', xml_attrs('disabled', sqldeep_json('true')))`
  Uses BLOB protocol (`sqldeep_json('true')`/`sqldeep_json('false')`) to distinguish booleans from plain integers.
  `sqldeep_json('true')` → bare attribute name; `sqldeep_json('false')` → omit; plain integer `1` → `name="1"`.
- XML inside JSON: `{ card: <div>{name}</div> }` → `sqldeep_json_object('card', xml_element('div', name))`
- JSON object inside XML: `<td>{{name, qty}}</td>` → `xml_element('td', sqldeep_json_object('name', name, 'qty', qty))`
- JSON path inside XML: `<td>{(data).field}</td>` → `xml_element('td', json_extract(CAST((data) AS TEXT), '$.field'))`
- Literal brace in XML: `{'{'}` → `'{'`
- JSON booleans: standalone `true`/`false` in JSON object fields, array elements, and
  XML attribute/interpolation contexts are auto-wrapped as `sqldeep_json('true')`/`sqldeep_json('false')`
  to preserve proper JSON boolean semantics (not integer 1/0).
- JSONML output: `jsonml(<div class="card">{name}</div>)` → `xml_element_jsonml('div', xml_attrs_jsonml('class', 'card'), name)` producing `["div",{"class":"card"},"alice"]`. Transpiler macro — emits `_jsonml` variant functions.
- JSONML subquery: `jsonml(<ul>{SELECT <li>{name}</li> FROM t}</ul>)` → uses `jsonml_agg` instead of `xml_agg`
- JSX output: `jsx(<Graph data={{x, y}} label="Sales"/>)` →
  `xml_element_jsx('Graph/', xml_attrs_jsx('data', sqldeep_json_object('x', x, 'y', y), 'label', 'Sales'))`.
  Like JSONML but preserves JSON-typed attribute values (BLOB protocol) as raw JSON
  in the attributes object instead of stringifying. `xml_attrs_jsx` checks BLOB type
  to detect JSON values. Boolean attributes (`<input disabled/>`) produce `{"disabled":true}` in JSX mode.
- JSX subquery: `jsx(<ul>{SELECT <li>{name}</li> FROM t}</ul>)` → uses `jsx_agg`
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
  nested elements, subqueries, namespaced tags, self-closing void elements,
  empty non-void elements, multi-line dedent, boolean attributes,
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
