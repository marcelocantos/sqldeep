# sqldeep — Stability

## Stability commitment

As of v1.0.0, the public API (`dist/sqldeep.h`), input syntax (the DSL), and
output semantics are a backwards-compatibility contract. Breaking changes
require a new major version.

### Stability levels

Each item in the catalogue below is marked with one of two levels:

- **Stable** — covered by the backwards-compatibility contract. Will not change
  in a backwards-incompatible way within the current major version. Removal or
  breaking changes require a new major version.
- **Experimental** — available for use but not yet part of the stability
  contract. May change in syntax, semantics, or output form in any minor
  release. Experimental items are promoted to Stable once the design is
  confirmed through real-world usage. Promotion is a one-way door — once
  Stable, an item cannot revert to Experimental.

New features land as Experimental. Existing Stable items are never affected by
the addition of Experimental features — the contract is per-item, not
per-release.

## Interaction surface catalogue

Snapshot as of v1.0.0.

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
| `SQLDEEP_VERSION` | `"1.0.0"` | **Stable** |
| `SQLDEEP_VERSION_MAJOR` | `1` | **Stable** |
| `SQLDEEP_VERSION_MINOR` | `0` | **Stable** |
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
| Line comments | `// comment` | **Stable** |
| Trailing commas | `{ id, name, }` | **Stable** |
| Plain FROM-first select | `FROM t SELECT id, name` | **Stable** |
| SQL passthrough | `SELECT id FROM t` | **Stable** |

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

## Design decisions

The following were evaluated during the pre-1.0 period and are now settled:

- **Join path syntax** (`->`, `<-`): Tested through v0.2.0–v0.6.0 with chains,
  bridges, ON/USING overrides, and FK-guided mode. The syntax is compact and
  unambiguous.
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

## Out of scope

- Schema-aware transpilation (accepting a database handle for automatic FK
  introspection — FK metadata can already be passed manually via
  `sqldeep_foreign_key`)
- Multi-statement input (`;`-separated)
- MySQL output target
- Custom aggregate functions beyond `json_group_array` / `jsonb_agg`
