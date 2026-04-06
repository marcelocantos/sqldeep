# sqldeep

A transpiler that converts JSON5-like SQL syntax into standard SQL with
database-specific JSON functions. Write nested JSON queries naturally; sqldeep
rewrites them into `sqldeep_json_object()` / `jsonb_build_object()`, etc. Supports
SQLite (default) and PostgreSQL backends.

## Example

**Input:**

```sql
FROM customers c SELECT {
    customers_id, name,
    orders: FROM c->orders o ORDER BY orders_id SELECT {
        orders_id, total,
        vendor: FROM o<-vendors v SELECT/1 { vendor_name: v.name },
    },
}
```

**Output:**

```sql
SELECT sqldeep_json_object('customers_id', customers_id, 'name', name, 'orders',
  (SELECT sqldeep_json_group_array(sqldeep_json_object('orders_id', orders_id, 'total', total, 'vendor',
    (SELECT sqldeep_json_object('vendor_name', v.name)
     FROM vendors v WHERE o.vendors_id = v.vendors_id LIMIT 1)))
   FROM orders o WHERE o.customers_id = c.customers_id ORDER BY orders_id))
FROM customers c
```

## Syntax

### FROM-first syntax

sqldeep supports both `SELECT { ... } FROM ...` and `FROM ... SELECT { ... }`.
The FROM-first form reads top-down — data source first, shape second:

```sql
FROM people p SELECT {
    id, name,
    orders: FROM orders o WHERE customer_id = p.id SELECT {
        order_id: id, total,
    },
}
```

Plain selects (no `{ }` or `[ ]`) are also supported — the clauses are simply
rearranged:

```sql
FROM orders WHERE total > 100 SELECT order_id, total
-- → SELECT order_id, total FROM orders WHERE total > 100
```

Both forms produce identical output and can be mixed freely.

### Object literals

Use `{ }` with `SELECT` to construct JSON objects:

```sql
SELECT { id, name } FROM users
-- or equivalently:
FROM users SELECT { id, name }
-- → SELECT sqldeep_json_object('id', id, 'name', name) FROM users
```

Three field forms:

| Form | Example | Output |
|---|---|---|
| Bare | `id,` | `'id', id` |
| Renamed | `order_id: id,` | `'order_id', id` |
| Quoted key | `"order id": id,` | `'order id', id` |

### Array literals

Use `[ ]` for inline JSON arrays:

```sql
SELECT { tags: [1, 2, 3] } FROM items
-- → SELECT sqldeep_json_object('tags', sqldeep_json_array(1, 2, 3)) FROM items
```

### Nested subqueries

When a field value is `SELECT { }` or `SELECT [ ]`, sqldeep wraps it as a
correlated subquery with `sqldeep_json_group_array()`:

```sql
SELECT {
    id,
    orders: SELECT { total } FROM orders WHERE cid = c.id,
} FROM customers c
-- → SELECT sqldeep_json_object('id', id, 'orders',
--     (SELECT sqldeep_json_group_array(sqldeep_json_object('total', total))
--      FROM orders WHERE cid = c.id))
--   FROM customers c
```

Nesting works to arbitrary depth.

### Singular select (`SELECT/1`)

Use `SELECT/1` for single-row projections. It skips `sqldeep_json_group_array()`
wrapping and appends `LIMIT 1`, returning a single object (or null) instead of
an array:

```sql
SELECT {
    orders_id,
    vendor: SELECT/1 { name } FROM o<-vendors v,
} FROM orders o
-- → SELECT sqldeep_json_object('orders_id', orders_id,
--     'vendor', (SELECT sqldeep_json_object('name', name)
--      FROM vendors v WHERE o.vendors_id = v.vendors_id LIMIT 1))
--   FROM orders o
```

Works with both `{ }` and `[ ]`, in SELECT-first and FROM-first syntax.

### Join paths (`->` and `<-`)

The `->` and `<-` operators generate JOIN/WHERE clauses from table relationships.
By default, they follow the convention that foreign keys are named `<table>_id`.
When FK metadata is provided (see [FK-guided joins](#fk-guided-joins)), real
column names are used instead.

```sql
-- One-to-many: customers → orders (orders has customers_id FK)
FROM c->orders o
-- → FROM orders o WHERE o.customers_id = c.customers_id

-- Many-to-one: orders → customers (orders has customers_id FK)
FROM o<-customers c
-- → FROM customers c WHERE o.customers_id = c.customers_id

-- Chain: customers → orders → items
FROM c->orders o->items i
-- → FROM orders o JOIN items i ON i.orders_id = o.orders_id
--   WHERE o.customers_id = c.customers_id

-- Bridge (many-to-many via junction table)
FROM c->custacct<-accounts a
-- → FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id
--   WHERE custacct.customers_id = c.customers_id
```

### FK-guided joins

Pass foreign key metadata to `sqldeep_transpile_fk()` to resolve join paths
from real FK information instead of the `<table>_id` convention. This supports
non-conventional column names and multi-column foreign keys:

```c
sqldeep_column_pair cols[] = {{"cust_id", "id"}};
sqldeep_foreign_key fks[] = {{"orders", "customers", cols, 1}};
char* sql = sqldeep_transpile_fk(input, fks, 1, &err_msg, &err_line, &err_col);
```

When FK metadata is provided, every join must be resolvable from it — there is
no fallback to the naming convention. Missing or ambiguous FKs return an error.

### Recursive tree construction

Build nested JSON trees from self-referential tables. The `*` marker declares
the fixed-point position — where children recurse with the same shape:

```sql
SELECT/1 {
  id, name,
  children: *
} FROM categories
  RECURSE ON (parent_id)
  WHERE parent_id IS NULL
```

This produces a single nested JSON tree with all descendants. Use `SELECT`
(without `/1`) for a forest (array of root trees). Specify an explicit PK
with `RECURSE ON (parent_id = category_id)` when the PK isn't `id`.

The output is generated entirely within SQL using a bracket-injection CTE
strategy — no client-side assembly required.

### XML/HTML literals

Produce well-formed markup directly from SQL queries. Designed for reactive UI
with [sqlpipe](https://github.com/marcelocantos/sqlpipe) — a single expression
both queries data and produces the HTML to render.

```sql
SELECT <table class="products">
  <tr><th>Item</th><th>Price</th></tr>
  {SELECT <tr>
    <td>{name}</td>
    <td>{'$' || price}</td>
  </tr> FROM items ORDER BY name}
</table>
-- → SELECT xml_element('table', xml_attrs('class', 'products'),
--     xml_element('tr', xml_element('th', 'Item'), xml_element('th', 'Price')),
--     (SELECT xml_agg(xml_element('tr',
--       xml_element('td', name), xml_element('td', '$' || price)))
--      FROM items ORDER BY name))
```

- `<tag attr="static" dynamic={expr}>...</tag>` → `xml_element('tag', xml_attrs(...), ...)`
- `{expr}` interpolation in content and attributes
- `{SELECT ...}` subqueries aggregate with `xml_agg()` instead of `sqldeep_json_group_array()`
- `{SELECT/1 ...}` singular subquery (no aggregation, adds `LIMIT 1`)
- Self-closing void elements: `<br/>`, `<img src={url}/>` — rendered as `<br/>` (never `<br></br>`)
- Non-void empty elements: `<div></div>` — rendered as `<div></div>` (never `<div/>`)
- Boolean attributes: `<input disabled/>` — uses `sqldeep_json('true')` to distinguish
  booleans from integers. In XML mode renders as `disabled`, in JSX mode as `true`.
- Namespaced tags: `<ui:Table.Cell>` for component frameworks
- Computed key: `{ (expr): val }` → `sqldeep_json_object(expr, val)` (runtime key)
- XML inside JSON: `{ name, card: <div>{name}</div> }` — XML expression as field value
- JSON inside XML: `<td>{{name, qty}}</td>` — double braces for JSON object interpolation
- JSON path in XML: `<td>{(data).field}</td>` — existing path syntax works inside interpolation
- Literal braces: `{'{'}` for a literal `{`
- Multi-line dedent: common leading whitespace is stripped, so source indentation
  produces relative indentation in output
- JSON booleans: standalone `true`/`false` in JSON object fields, array elements,
  and XML contexts are auto-wrapped as `sqldeep_json('true')`/`sqldeep_json('false')` to preserve
  proper JSON boolean semantics (emits `true`/`false` not `1`/`0`)

#### Output modes

Three output modes via pseudo-function wrappers:

| Syntax | Attribute values | Use case |
|---|---|---|
| `<div>...</div>` | Always strings (HTML) | Server-rendered HTML |
| `jsonml(<div>...</div>)` | Always strings | Structural JSONML format |
| `jsx(<div>...</div>)` | JSON-valued attrs preserved | Component rendering (React, etc.) |

**JSONML** — wraps XML in `jsonml(...)` to produce
[JSONML](http://www.jsonml.org/) arrays instead of XML strings:

```sql
SELECT jsonml(
  <ul>{SELECT <li>{name}</li> FROM items ORDER BY name}</ul>
)
-- → ["ul",["li","apple"],["li","banana"]]
```

**JSX** — like JSONML, but preserves JSON-typed attribute values (objects,
arrays, booleans) as live JSON instead of stringifying them:

```sql
SELECT jsx(<Graph data={{x, y}} animated/>)
-- → ["Graph",{"data":{"x":1,"y":2},"animated":true}]
```

These are transpiler-level macros — no runtime XML parsing. The transpiler emits
mode-specific function variants (`xml_element_jsonml`/`xml_element_jsx`, etc.).

The XML functions (`xml_element`, `xml_attrs`, `xml_agg` and their `_jsonml`/`_jsx`
counterparts) are runtime concerns — sqldeep only emits calls to them. The
`sqldeep` CLI includes reference implementations for interactive use.

### Comments and trailing commas

- `--` line comments and `/* ... */` block comments are stripped
- Trailing commas are allowed in objects and arrays

### SQL passthrough

Any SQL that doesn't contain `{ }` or `[ ]` constructs passes through
unchanged.

## API

The public interface is a C header (`sqldeep.h`) suitable for direct use from
C/C++ or via FFI (Go/cgo, Rust, Python, etc.):

```c
#include "sqldeep.h"

char* err_msg = NULL;
int err_line = 0, err_col = 0;

// Convention-based (uses <table>_id for join paths):
char* sql = sqldeep_transpile("SELECT { id, name } FROM users",
                               &err_msg, &err_line, &err_col);
// Use sql... then free:
sqldeep_free(sql);

// FK-guided (uses explicit foreign key metadata):
sqldeep_column_pair cols[] = {{"cust_id", "id"}};
sqldeep_foreign_key fks[] = {{"orders", "customers", cols, 1}};
char* sql2 = sqldeep_transpile_fk(input, fks, 1,
                                    &err_msg, &err_line, &err_col);

// PostgreSQL backend:
char* sql3 = sqldeep_transpile_backend(input, SQLDEEP_POSTGRES,
                                        &err_msg, &err_line, &err_col);
```

On error, functions return `NULL` and set the `err_msg`, `err_line`, and
`err_col` out-parameters. Free returned strings (including error messages) with
`sqldeep_free()`.

## Building

```sh
git clone https://github.com/marcelocantos/sqldeep.git
cd sqldeep
mk test     # run tests
mk example  # run the demo
```

Requires C++20 and [mk](https://github.com/marcelocantos/mk).

### Interactive CLI

```sh
mk shell                       # build the CLI
cp build/sqldeep ~/.local/bin/  # install
sqldeep mydb.db                 # interactive session
```

The `sqldeep` binary is a full SQLite shell (readline, dot-commands, all flags)
with sqldeep transpilation and XML functions (`xml_element`, `xml_attrs`,
`xml_agg`) built in.

## Integration

Copy `dist/sqldeep.h` and `dist/sqldeep.cpp` into your project and compile as
C++20. No external dependencies at runtime.

If your project uses XML literals and targets SQLite, also include
`dist/sqldeep_xml.h` and `dist/sqldeep_xml.c`. These provide reference
implementations of `xml_element`, `xml_attrs`, and `xml_agg` as SQLite custom
functions. Register them on each connection:

```c
#include "sqldeep_xml.h"

sqldeep_register_sqlite_xml(db);  // returns SQLITE_OK on success
```

`sqldeep_xml.c` compiles as C and requires `sqlite3.h` at compile time.

If you use an agentic coding tool, include `dist/sqldeep-agents-guide.md` in
your project context.

## Related projects

- **[sqlift](https://github.com/marcelocantos/sqlift)** — Declarative SQLite schema migrations. Describe your desired schema; sqlift diffs and applies the changes.
- **[sqlpipe](https://github.com/marcelocantos/sqlpipe)** — Streaming SQLite replication protocol. Keeps two databases in sync over any transport.

## License

Apache 2.0. See [LICENSE](LICENSE).
