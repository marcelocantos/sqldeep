# sqldeep — Stability

## Stability commitment

Once sqldeep reaches 1.0, its public API (the `dist/sqldeep.h` header), input syntax
(the DSL), and output semantics become a backwards-compatibility contract.
Breaking changes after 1.0 require forking into a new project. The pre-1.0
period exists to get the API and syntax right.

## Interaction surface catalogue

Snapshot as of v0.5.0.

### C API (`sqldeep.h`)

| Item | Signature | Stability |
|------|-----------|-----------|
| `sqldeep_transpile` | `char* sqldeep_transpile(const char* input, char** err_msg, int* err_line, int* err_col)` | **Stable** |
| `sqldeep_transpile_fk` | `char* sqldeep_transpile_fk(const char* input, const sqldeep_foreign_key* fks, int fk_count, char** err_msg, int* err_line, int* err_col)` | **Needs review** |
| `sqldeep_transpile_backend` | `char* sqldeep_transpile_backend(const char* input, sqldeep_backend backend, char** err_msg, int* err_line, int* err_col)` | **Needs review** |
| `sqldeep_transpile_fk_backend` | `char* sqldeep_transpile_fk_backend(...)` | **Needs review** |
| `sqldeep_foreign_key` | `struct { from_table, to_table, columns, column_count }` | **Needs review** |
| `sqldeep_column_pair` | `struct { from_column, to_column }` | **Needs review** |
| `sqldeep_backend` | `enum { SQLDEEP_SQLITE, SQLDEEP_POSTGRES }` | **Needs review** |
| `sqldeep_version` | `const char* sqldeep_version(void)` | **Stable** |
| `sqldeep_free` | `void sqldeep_free(void* ptr)` | **Stable** |

### Version macros (`sqldeep.h`)

| Macro | Value | Stability |
|-------|-------|-----------|
| `SQLDEEP_VERSION` | `"0.5.0"` | **Stable** |
| `SQLDEEP_VERSION_MAJOR` | `0` | **Stable** |
| `SQLDEEP_VERSION_MINOR` | `5` | **Stable** |
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
| Singular object select | `SELECT/1 { id } FROM t` | **Needs review** |
| Singular array select | `SELECT/1 [expr] FROM t` | **Needs review** |
| FROM-first singular select | `FROM t SELECT/1 { id }` | **Needs review** |
| Inline array | `[expr, ...]` | **Stable** |
| Inline object | `{ fields }` | **Stable** |
| Bare field | `id,` | **Stable** |
| Renamed field | `order_id: id` | **Stable** |
| Double-quoted key | `"order id": id` | **Stable** |
| Forward join (`->`) | `FROM c->orders o` | **Needs review** |
| Reverse join (`<-`) | `FROM o<-customers c` | **Needs review** |
| Join path chain | `FROM c->orders o->items i` | **Needs review** |
| Bridge join | `FROM c->custacct<-accounts a` | **Needs review** |
| ON clause | `c->orders o ON id = cust_id` | **Needs review** |
| USING clause | `c->orders o USING (person_id)` | **Needs review** |
| JSON path extraction | `(expr).field[n]` | **Needs review** |
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
| Singular select | `json_object(...) ... LIMIT 1` (no array wrapping) | **Needs review** |
| Inline array | `json_array(...)` | **Stable** |
| Forward join | `FROM table WHERE table.parent_id = alias.parent_id` | **Needs review** |
| Reverse join | `FROM table WHERE alias.table_id = table.table_id` | **Needs review** |
| Join path chain | `FROM t1 JOIN t2 ON ... WHERE ...` | **Needs review** |
| Plain FROM-first select | `SELECT expr FROM ...` (rearranged, no JSON) | **Stable** |
| ON/USING override | `o.cust_id = c.id` (explicit column pair) | **Needs review** |
| JSON path (SQLite) | `json_extract(expr, '$.field[n]')` | **Needs review** |
| JSON path (PostgreSQL) | `jsonb_extract_path(expr, 'field', 'n')` | **Needs review** |
| FK convention | `<table>_id` column naming | **Needs review** |
| FK-guided join | Uses explicit FK metadata (no convention fallback) | **Needs review** |
| Multi-column FK | AND-joined conditions | **Needs review** |

## Gaps and prerequisites

Before 1.0:

- **Join path syntax settling**: The `->` and `<-` operators, chains, bridge
  patterns, and ON/USING overrides need real-world usage to confirm the
  syntax, FK convention, and rendering are right.
- **Singular select (`SELECT/1`)**: New in v0.4.0. The `/1` suffix is compact
  but unusual — needs usage feedback to confirm it's the right spelling.
- **JSON path extraction**: New in v0.5.0. The `(expr).path` syntax needs
  real-world usage to confirm it covers common patterns.
- **PostgreSQL backend**: New in v0.5.0. The `sqldeep_backend` enum and
  `_backend` function variants need usage to confirm the API shape is right.
- **FK-guided joins**: The `ForeignKey` struct and FK-aware `transpile()`
  overload need real-world usage to confirm the API shape is right. Ambiguous FK
  disambiguation (multiple FKs between the same tables) is not yet supported in
  the syntax — users must use explicit WHERE clauses for now.
- **Error messages**: Error text is not part of the stability contract, but
  should be consistently helpful before 1.0.
- **Distribution story**: No install target, pkg-config, or CMake find-module.
  Users copy two files. This may be fine for 1.0 but should be a conscious
  decision.

## Out of scope for 1.0

- Schema-aware transpilation (accepting a database handle for automatic FK
  introspection — FK metadata can already be passed manually via
  `sqldeep_foreign_key`)
- Multi-statement input (`;`-separated)
- MySQL output target
- Aggregate functions beyond `json_group_array` / `jsonb_agg`
