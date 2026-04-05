# sqldeep — Agent Guide

Transpiler: JSON5-like SQL syntax → standard SQL with database-specific JSON
functions. C header with C++20 implementation. Zero runtime dependencies.
Supports SQLite (default) and PostgreSQL backends.

## Integration

Copy the `dist/` files (`sqldeep.h` and `sqldeep.cpp`) into your project and
compile as C++20.
No link-time dependencies — the database is only needed at runtime by the
*caller*, not by sqldeep itself.

If your project uses XML literals and targets SQLite, also include
`sqldeep_xml.h` and `sqldeep_xml.c`. These provide the `xml_element`,
`xml_attrs`, and `xml_agg` SQLite functions that the transpiled SQL calls:

```c
#include "sqldeep_xml.h"

sqldeep_register_sqlite_xml(db);  // call once per connection
```

`sqldeep_xml.c` compiles as C (not C++20) and requires `sqlite3.h` at compile
time and the SQLite library at link time.

## API

The public interface is a C header (`sqldeep.h`), usable from C/C++ or via FFI:

```c
#include "sqldeep.h"

char* err_msg = NULL;
int err_line = 0, err_col = 0;

// Convention-based (uses <table>_id for join paths):
char* sql = sqldeep_transpile(input, &err_msg, &err_line, &err_col);
sqldeep_free(sql);

// FK-guided (uses explicit foreign key metadata, no convention fallback):
sqldeep_column_pair cols[] = {{"cust_id", "id"}};
sqldeep_foreign_key fks[] = {{"orders", "customers", cols, 1}};
char* sql2 = sqldeep_transpile_fk(input, fks, 1,
                                    &err_msg, &err_line, &err_col);

// PostgreSQL backend:
char* sql3 = sqldeep_transpile_backend(input, SQLDEEP_POSTGRES,
                                        &err_msg, &err_line, &err_col);
```

### Error handling

On error, functions return `NULL` and set the out-parameters:

```c
char* err_msg = NULL;
int err_line = 0, err_col = 0;
char* sql = sqldeep_transpile(input, &err_msg, &err_line, &err_col);
if (!sql) {
    // err_msg  — error message (free with sqldeep_free)
    // err_line — 1-based line number
    // err_col  — 1-based column number
    sqldeep_free(err_msg);
}
```

### Version macros

```c
SQLDEEP_VERSION       // "0.14.0"
SQLDEEP_VERSION_MAJOR // 0
SQLDEEP_VERSION_MINOR // 14
SQLDEEP_VERSION_PATCH // 0
const char* sqldeep_version(void);  // returns SQLDEEP_VERSION
```

## Syntax quick reference

Both SELECT-first and FROM-first syntax are supported (identical output):

| Input pattern | Output |
|---|---|
| `SELECT { fields } FROM ...` | `SELECT json_object(...) FROM ...` |
| `FROM ... SELECT { fields }` | same (FROM-first alternative) |
| Nested deep select | `(SELECT json_group_array(json_object(...)) FROM ...)` |
| `SELECT [expr] FROM ...` | `SELECT json_group_array(expr) FROM ...` |
| `FROM ... SELECT [expr]` | same (FROM-first alternative) |
| `SELECT/1 { fields } FROM ...` | `SELECT json_object(...) FROM ... LIMIT 1` (singular) |
| `SELECT/1 [expr] FROM ...` | `SELECT expr FROM ... LIMIT 1` (singular) |
| `FROM ... SELECT/1 { fields }` | same (FROM-first singular) |
| `FROM ... SELECT expr` | `SELECT expr FROM ...` (plain rearrangement) |
| `[expr, ...]` | `json_array(...)` |
| `{ fields }` (inline) | `json_object(...)` |

### Field forms

| Form | Example | Emits |
|---|---|---|
| Bare | `id,` | `'id', id` |
| Renamed | `order_id: id` | `'order_id', id` |
| Quoted key | `"order id": id` | `'order id', id` |
| Computed key | `(expr): val` | `expr, val` |
| Aggregate | `field: SELECT expr` | `'field', json_group_array(expr)` |
| Singular aggregate | `field: SELECT/1 expr` | `'field', expr` |

- Join paths:
  - `->` (one-to-many): `FROM c->orders o` → `FROM orders o WHERE o.customers_id = c.customers_id`
  - `<-` (many-to-one): `FROM o<-customers c` → `FROM customers c WHERE o.customers_id = c.customers_id`
  - Chains: `FROM c->orders o->items i` → `FROM orders o JOIN items i ON i.orders_id = o.orders_id WHERE o.customers_id = c.customers_id`
  - Bridge (many-to-many): `FROM c->custacct<-accounts a` → `FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id WHERE custacct.customers_id = c.customers_id`
  - ON/USING overrides: `c->orders o ON id = cust_id` → `o.cust_id = c.id`;
    `c->orders o USING (person_id)` → `o.person_id = c.person_id`.
    Each step in a chain can have its own ON/USING clause; steps without fall back to convention/FK.
  - Convention mode (default): PK = `<table>_id`, FK = same column name in child/parent table.
  - FK-guided mode: pass `sqldeep_foreign_key` array to `sqldeep_transpile_fk()`. Supports multi-column FKs. Errors on missing/ambiguous FK (no convention fallback).
- JSON path extraction: `(expr).field.sub[n]` → `json_extract(expr, '$.field.sub[n]')` (SQLite)
  / `jsonb_extract_path(expr, 'field', 'sub', 'n')` (PostgreSQL).
  Parentheses around the base expression disambiguate from `table.column`.
- Recursive tree construction: `SELECT/1 { id, name, children: * } FROM t RECURSE ON (parent_id) WHERE parent_id IS NULL`
  produces nested JSON trees from self-referential tables. `*` marks the recursion point.
  `RECURSE ON (fk = pk)` for explicit PK (default: `id`). `SELECT` (no `/1`) produces a forest `[]`.
  Output is a bracket-injection CTE — no client-side assembly.
- XML element: `<div class="card">{name}</div>` → `xml_element('div', xml_attrs('class', 'card'), name)`
- XML self-closing: `<br/>` → `xml_element('br/')` (trailing `/` in tag name signals void element)
- XML empty non-void: `<div></div>` → `xml_element('div')` (renders `<div></div>`, not `<div/>`)
- XML multi-line dedent: common leading-space prefix across lines is stripped,
  so source indentation produces relative indentation in output
- XML interpolation: `{expr}` inside XML content or attributes
- XML subquery: `{SELECT <li>{name}</li> FROM t}` → `(SELECT xml_agg(xml_element('li', name)) FROM t)`
- XML singular subquery: `{SELECT/1 <span>{name}</span> FROM t}` → `(SELECT xml_element(...) FROM t LIMIT 1)`
- XML namespaced tags: `<ui:Table.Cell>` → `xml_element('ui:Table.Cell', ...)`
- XML boolean attribute: `<input disabled/>` → `xml_attrs('disabled', json('true'))`.
  Uses `json('true')`/`json('false')` (subtype 74) to distinguish booleans from integers.
- XML inside JSON: `{ card: <div>{name}</div> }` → `json_object('card', CAST(xml_element(...) AS TEXT))`
- JSON inside XML: `<td>{{name, qty}}</td>` → `xml_element('td', json_object(...))`
- JSON path inside XML: `<td>{(data).field}</td>` → `xml_element('td', json_extract(data, '$.field'))`
- Literal brace in XML: `{'{'}` → `'{'`
- JSON booleans: standalone `true`/`false` in JSON object fields, array elements,
  and XML attribute/interpolation contexts are auto-wrapped as `json('true')`/`json('false')`
  to preserve proper JSON boolean semantics (not integer 1/0).
- JSONML output: `jsonml(<div class="card">{name}</div>)` → `CAST(xml_element_jsonml('div', xml_attrs_jsonml('class', 'card'), name) AS TEXT)` producing `["div",{"class":"card"},"alice"]`
- JSONML subquery: `jsonml(<ul>{SELECT <li>{name}</li> FROM t}</ul>)` → uses `jsonml_agg` instead of `xml_agg`
- JSX output: `jsx(<Graph data={{x, y}} label="Sales"/>)` → `CAST(xml_element_jsx('Graph/', xml_attrs_jsx('data', json_object(...), 'label', 'Sales')) AS TEXT)`.
  Like JSONML but preserves JSON-typed attribute values (subtype 74) as raw JSON
  in the attributes object. Boolean attributes (`<input disabled/>`) produce `{"disabled":true}` in JSX mode.
- JSX subquery: `jsx(<ul>{SELECT <li>{name}</li> FROM t}</ul>)` → uses `jsx_agg`
- `--` line comments and `/* ... */` block comments are stripped.
- Trailing commas are allowed in objects and arrays.
- SQL without `{ }`, `[ ]`, or `<tag>` constructs passes through unchanged.

## Gotchas

- **C++20 required** for compilation. The public API is C.
- **No database dependency.** sqldeep is a pure string→string transform. The
  caller is responsible for executing the output SQL.
- **Nesting depth limit.** Constructs nested beyond 200 levels return an error.
- **Single-quoted strings are SQL strings** (passed through verbatim).
  **Double-quoted strings are JSON keys** (converted to single-quoted keys in
  output). Don't confuse them.
- **Commas in field values.** The parser tracks parenthesis depth to
  distinguish structural commas (field separators) from commas inside
  expressions like function calls. Bare commas outside parentheses end a field
  value.
- **Subquery wrapping.** `(SELECT { ... })`, `(FROM ... SELECT { ... })`, and
  `(FROM ... SELECT expr)` in parentheses are recognised specially — sqldeep
  avoids double-wrapping with extra parens.
- **FK-guided mode is strict.** When FK metadata is provided, every join
  must be resolvable from it — no fallback to the `<table>_id` convention.
  Multiple FKs between the same table pair cause an ambiguity error.
- **Free returned strings.** All strings returned by `sqldeep_transpile*`
  (including error messages) must be freed with `sqldeep_free()`.

## Common patterns

### Basic (FROM-first)

```
FROM users WHERE active = 1 SELECT { id, name, email }
```

### Nested one-to-many (auto-join, FROM-first)

```
FROM customers c SELECT {
    customers_id, name,
    orders: FROM c->orders o ORDER BY orders_id SELECT {
        orders_id,
        items: FROM o->items ORDER BY items_id SELECT { name, qty },
    },
}
```

### Grandchild chain (one-to-many-to-many)

```
FROM customers c SELECT {
    customers_id, name,
    items: FROM c->orders o->items i SELECT { items_id, name },
}
```

### Many-to-many via bridge table

```
FROM customers c SELECT {
    customers_id, name,
    accounts: FROM c->custacct<-accounts a SELECT { acct_name },
}
```

### Singular select (one-to-one / many-to-one)

```
FROM orders o SELECT {
    orders_id,
    vendor: SELECT/1 { name } FROM o<-vendors v,
}
```

Returns a single object (or null if no match) instead of an array. Use for
relationships where you expect at most one result.

### Many-to-one (reverse join)

```
FROM orders o SELECT {
    orders_id,
    customer: FROM o<-customers c SELECT { name },
}
```

### Nested one-to-many (explicit WHERE)

```
FROM customers c SELECT {
    id, name,
    orders: FROM orders o WHERE o.cid = c.id SELECT { order_id: id, total },
}
```

### Inline array

```
FROM items SELECT { tags: [1, 'two', 3] }
```

### FK-guided join (non-conventional column names)

```c
sqldeep_column_pair cols[] = {{"cust_id", "id"}};
sqldeep_foreign_key fks[] = {{"orders", "customers", cols, 1}};
char* sql = sqldeep_transpile_fk(
    "FROM customers c SELECT { name, "
    "orders: FROM c->orders o SELECT { total } }",
    fks, 1, &err_msg, &err_line, &err_col);
// → o.cust_id = c.id (instead of o.customers_id = c.customers_id)
```

### ON/USING overrides (non-conventional FK columns)

```
FROM customers c SELECT {
    name,
    orders: FROM c->orders o ON id = cust_id SELECT { total },
}
// → o.cust_id = c.id (instead of o.customers_id = c.customers_id)
```

### JSON path extraction

```
FROM events SELECT { type: (data).event_type }
// SQLite:      → json_extract(data, '$.event_type')
// PostgreSQL:  → jsonb_extract_path(data, 'event_type')
```

### Aggregate fields (GROUP BY projection)

```
FROM t GROUP BY grp SELECT {
    grp,
    names: SELECT name,
    total: SELECT/1 sum(val),
}
// → json_object('grp', grp, 'names', json_group_array(name), 'total', sum(val))
```

`SELECT expr` (no FROM) wraps in `json_group_array()`; `SELECT/1 expr` emits
the expression directly (useful for aggregate functions like `sum`/`count`).

### Computed keys (dynamic JSON key names)

```
FROM t SELECT { (tag): score }
// → json_object(tag, score) — key is the runtime value of `tag`
```

### SELECT-first (also supported)

```
SELECT { id, name } FROM users
SELECT { id, orders: SELECT { total } FROM orders WHERE cid = c.id } FROM customers c
```
