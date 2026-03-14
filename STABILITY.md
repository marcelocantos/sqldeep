# sqldeep — Stability

## Stability commitment

Once sqldeep reaches 1.0, its public API (`dist/sqldeep.h`), input syntax
(the DSL), and output semantics become a backwards-compatibility contract.
Breaking changes after 1.0 require a new major version. The pre-1.0 period
exists to get the API and syntax right.

### Stability levels

Each item in the catalogue below is marked with one of three levels:

- **Stable** — design is settled and well-tested. Unlikely to change before
  1.0, and will not change in a backwards-incompatible way after 1.0.
- **Needs review** — functional but may benefit from refinement before locking
  in. May change in any pre-1.0 release.
- **Experimental** — new or recently changed. Needs real-world usage to confirm
  the design. May change in any pre-1.0 release.

Post-1.0, Stable items are covered by the backwards-compatibility contract.
Experimental items may still change in minor releases. Promotion from
Experimental to Stable is a one-way door.

## Interaction surface catalogue

Snapshot as of v0.7.0 (unreleased).

### C API (`sqldeep.h`)

| Item | Signature | Stability |
|------|-----------|-----------|
| `sqldeep_transpile` | `char* sqldeep_transpile(const char* input, char** err_msg, int* err_line, int* err_col)` | **Stable** |
| `sqldeep_transpile_fk` | `char* sqldeep_transpile_fk(const char* input, const sqldeep_foreign_key* fks, int fk_count, char** err_msg, int* err_line, int* err_col)` | **Stable** |
| `sqldeep_transpile_backend` | `char* sqldeep_transpile_backend(const char* input, sqldeep_backend backend, char** err_msg, int* err_line, int* err_col)` | **Stable** |
| `sqldeep_transpile_fk_backend` | `char* sqldeep_transpile_fk_backend(...)` | **Stable** |
| `sqldeep_foreign_key` | `struct { from_table, to_table, columns, column_count }` | **Stable** |
| `sqldeep_column_pair` | `struct { from_column, to_column }` | **Stable** |
| `sqldeep_backend` | `enum { SQLDEEP_SQLITE, SQLDEEP_POSTGRES }` | **Stable** |
| `sqldeep_version` | `const char* sqldeep_version(void)` | **Stable** |
| `sqldeep_free` | `void sqldeep_free(void* ptr)` | **Stable** |

### Version macros (`sqldeep.h`)

| Macro | Value | Stability |
|-------|-------|-----------|
| `SQLDEEP_VERSION` | `"0.7.0"` | **Stable** |
| `SQLDEEP_VERSION_MAJOR` | `0` | **Stable** |
| `SQLDEEP_VERSION_MINOR` | `7` | **Stable** |
| `SQLDEEP_VERSION_PATCH` | `0` | **Stable** |

### Input syntax (DSL)

| Construct | Example | Stability |
|-----------|---------|-----------|
| Object select | `SELECT { id, name } FROM t` | **Stable** |
| FROM-first object select | `FROM t SELECT { id, name }` | **Stable** |
| Nested object select | `field: SELECT { ... } FROM t` | **Stable** |
| Nested FROM-first select | `field: FROM t SELECT { ... }` | **Stable** |
| Array select | `SELECT [expr] FROM t` | **Stable** |
| FROM-first array select | `FROM t SELECT [expr]` | **Stable** |
| Singular object select | `SELECT/1 { id } FROM t` | **Stable** |
| Singular array select | `SELECT/1 [expr] FROM t` | **Stable** |
| FROM-first singular select | `FROM t SELECT/1 { id }` | **Stable** |
| Inline array | `[expr, ...]` | **Stable** |
| Inline object | `{ fields }` | **Stable** |
| Bare field | `id,` | **Stable** |
| Renamed field | `order_id: id` | **Stable** |
| Double-quoted key | `"order id": id` | **Stable** |
| Computed key | `(expr): value` | **Stable** |
| Aggregate field | `field: SELECT expr` (no FROM) | **Stable** |
| Singular aggregate field | `field: SELECT/1 expr` (no FROM) | **Stable** |
| Forward join (`->`) | `FROM c->orders o` | **Stable** |
| Reverse join (`<-`) | `FROM o<-customers c` | **Stable** |
| Join path chain | `FROM c->orders o->items i` | **Stable** |
| Bridge join | `FROM c->custacct<-accounts a` | **Stable** |
| ON clause | `c->orders o ON id = cust_id` | **Stable** |
| USING clause | `c->orders o USING (person_id)` | **Stable** |
| JSON path extraction | `(expr).field[n]` | **Stable** |
| Line comments | `-- comment` | **Experimental** |
| Block comments | `/* comment */` | **Experimental** |
| Trailing commas | `{ id, name, }` | **Stable** |
| Plain FROM-first select | `FROM t SELECT id, name` | **Stable** |
| SQL passthrough | `SELECT id FROM t` | **Stable** |
| SQL `->` / `->>` passthrough | `data->>'name'` in non-FROM context | **Experimental** |
| Recursive select | `SELECT { ..., children: * } FROM t RECURSE ON (fk)` | **Experimental** |
| Recursive explicit PK | `RECURSE ON (fk = pk)` | **Experimental** |
| Recursive singular | `SELECT/1 { ..., children: * } FROM t RECURSE ON (fk)` | **Experimental** |

### Output semantics

| Input construct | Output | Stability |
|-----------------|--------|-----------|
| Object select | `json_object(...)` | **Stable** |
| Nested object select | `(SELECT json_group_array(json_object(...)))` | **Stable** |
| Array select | `json_group_array(...)` | **Stable** |
| Singular select | `json_object(...) ... LIMIT 1` (no array wrapping) | **Stable** |
| Inline array | `json_array(...)` | **Stable** |
| Computed key | `expr, value` (key is runtime expression) | **Stable** |
| Aggregate field | `json_group_array(expr)` | **Stable** |
| Singular aggregate field | `expr` (no array wrapping) | **Stable** |
| Forward join | `FROM table WHERE table.parent_id = alias.parent_id` | **Stable** |
| Reverse join | `FROM table WHERE alias.table_id = table.table_id` | **Stable** |
| Join path chain | `FROM t1 JOIN t2 ON ... WHERE ...` | **Stable** |
| Plain FROM-first select | `SELECT expr FROM ...` (rearranged, no JSON) | **Stable** |
| ON/USING override | `o.cust_id = c.id` (explicit column pair) | **Stable** |
| JSON path (SQLite) | `json_extract(expr, '$.field[n]')` | **Stable** |
| JSON path (PostgreSQL) | `jsonb_extract_path(expr, 'field', 'n')` | **Stable** |
| FK convention | `<table>_id` column naming | **Stable** |
| FK-guided join | Uses explicit FK metadata (no convention fallback) | **Stable** |
| Multi-column FK | AND-joined conditions | **Stable** |
| Recursive select | `WITH RECURSIVE` 3-CTE bracket-injection template | **Experimental** |

### Parser behaviour

| Behaviour | Description | Stability |
|-----------|-------------|-----------|
| `->` / `<-` FROM-only | Join arrows only recognised after FROM/JOIN | **Experimental** |

## Design decisions

The following were evaluated during the pre-1.0 period and are now settled:

- **Join path syntax** (`->`, `<-`): Tested through v0.2.0–v0.6.0 with chains,
  bridges, ON/USING overrides, and FK-guided mode. The syntax is compact and
  unambiguous within FROM context.
- **Singular select** (`SELECT/1`): The `/1` suffix is unusual but compact,
  consistent with the DSL's style, and has no parsing ambiguity.
- **JSON path extraction** (`(expr).path`): Parentheses disambiguate from
  `table.column`. Works at any paren depth.
- **Computed keys** (`(expr): value`): Parentheses disambiguate from bare field
  names. Resolves the JSON5 key ambiguity (issue #10).
- **Aggregate fields** (`field: SELECT expr`): Clean reuse of SELECT keyword
  for in-scope aggregation.
- **PostgreSQL backend**: The `sqldeep_backend` enum is extensible for future
  backends without API changes.
- **FK-guided joins**: The `sqldeep_foreign_key` struct supports multi-column
  FKs. Ambiguous FK disambiguation (multiple FKs between the same tables) is
  handled via explicit ON/USING clauses rather than syntax extensions.
- **Distribution**: Two-file model (`sqldeep.h` + `sqldeep.cpp`). No pkg-config
  or CMake find-module — users copy files directly. This matches the
  header-only library convention and keeps integration simple.
- **Error messages**: Error text is not part of the stability contract. Messages
  may be improved in minor releases.
- **Comment syntax** (v0.7.0): Replaced `//` (JSON5-style) with SQL-standard
  `--` line comments and `/* */` block comments. The `//` style served no
  purpose in a language that already has standard comment syntax via its SQL
  host. Block comments are flat (non-nested), matching SQLite semantics.
- **`->` FROM-context restriction** (v0.7.0): The `->` and `<-` join operators
  are now only recognised after FROM/JOIN keywords, resolving a conflict with
  SQLite's `->` and `->>` JSON extraction operators. Previously, `->` after
  any identifier at paren depth 0 was treated as a join arrow, which prevented
  use of SQL JSON operators in SELECT, WHERE, and other non-FROM contexts.

## Gaps and prerequisites

Before 1.0:

- **Comment syntax settling**: `--` and `/* */` replaced `//` in v0.7.0.
  This is a breaking change from v0.6.0. Needs real-world usage to confirm
  the change was the right call and that no edge cases remain.
- **`->` FROM-context restriction settling**: Changed parser behaviour in
  v0.7.0. Needs usage to confirm no false negatives (legitimate join arrows
  missed) or false positives (non-join `->` incorrectly consumed).
- **`->>` passthrough settling**: New lexer behaviour in v0.7.0. The lexer
  extends `->` to `->>` when the `>` is touching. Needs usage to confirm
  this heuristic is robust.
- **Recursive select settling**: New in v0.8.0. `RECURSE ON` syntax, `*`
  field marker, and bracket-injection CTE output all need real-world usage.
  Current limitations: top-level only (not nested), integer PKs only,
  no FROM-first variant, no renamed/computed fields within the recursive shape.

## Known limitations

- **Bracket-quoted identifiers** (`[column name]`): Not supported. The `[`
  character is used for array literals in sqldeep syntax and cannot be
  disambiguated from bracket-quoted identifiers at the lexer level. Use
  double-quoted identifiers (`"column name"`) instead — this is the SQL
  standard and works in all supported backends.

## Out of scope for 1.0

- Schema-aware transpilation (accepting a database handle for automatic FK
  introspection — FK metadata can already be passed manually via
  `sqldeep_foreign_key`)
- Multi-statement input (`;`-separated)
- MySQL output target
- Custom aggregate functions beyond `json_group_array` / `jsonb_agg`
