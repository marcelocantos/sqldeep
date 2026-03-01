# sqldeep

A C++ transpiler that converts JSON5-like SQL syntax into standard SQLite JSON
functions. Write nested JSON queries naturally; sqldeep rewrites them into
`json_object()`, `json_array()`, and `json_group_array()` calls.

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
SELECT json_object('customers_id', customers_id, 'name', name, 'orders',
  (SELECT json_group_array(json_object('orders_id', orders_id, 'total', total, 'vendor',
    (SELECT json_object('vendor_name', v.name)
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
-- → SELECT json_object('id', id, 'name', name) FROM users
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
-- → SELECT json_object('tags', json_array(1, 2, 3)) FROM items
```

### Nested subqueries

When a field value is `SELECT { }` or `SELECT [ ]`, sqldeep wraps it as a
correlated subquery with `json_group_array()`:

```sql
SELECT {
    id,
    orders: SELECT { total } FROM orders WHERE cid = c.id,
} FROM customers c
-- → SELECT json_object('id', id, 'orders',
--     (SELECT json_group_array(json_object('total', total))
--      FROM orders WHERE cid = c.id))
--   FROM customers c
```

Nesting works to arbitrary depth.

### Singular select (`SELECT/1`)

Use `SELECT/1` for single-row projections. It skips `json_group_array()`
wrapping and appends `LIMIT 1`, returning a single object (or null) instead of
an array:

```sql
SELECT {
    orders_id,
    vendor: SELECT/1 { name } FROM o<-vendors v,
} FROM orders o
-- → SELECT json_object('orders_id', orders_id,
--     'vendor', (SELECT json_object('name', name)
--      FROM vendors v WHERE o.vendors_id = v.vendors_id LIMIT 1))
--   FROM orders o
```

Works with both `{ }` and `[ ]`, in SELECT-first and FROM-first syntax.

### Join paths (`->` and `<-`)

The `->` and `<-` operators generate JOIN/WHERE clauses from table relationships,
following the convention that foreign keys are named `<table>_id`:

```sql
// One-to-many: customers → orders (orders has customers_id FK)
FROM c->orders o
-- → FROM orders o WHERE o.customers_id = c.customers_id

// Many-to-one: orders → customers (orders has customers_id FK)
FROM o<-customers c
-- → FROM customers c WHERE o.customers_id = c.customers_id

// Chain: customers → orders → items
FROM c->orders o->items i
-- → FROM orders o JOIN items i ON i.orders_id = o.orders_id
--   WHERE o.customers_id = c.customers_id

// Bridge (many-to-many via junction table)
FROM c->custacct<-accounts a
-- → FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id
--   WHERE custacct.customers_id = c.customers_id
```

### Comments and trailing commas

- `//` line comments are stripped
- Trailing commas are allowed in objects and arrays

### SQL passthrough

Any SQL that doesn't contain `{ }` or `[ ]` constructs passes through
unchanged.

## API

```cpp
#include "sqldeep.h"

std::string sql = sqldeep::transpile(R"(
    SELECT { id, name } FROM users
)");
// sql == "SELECT json_object('id', id, 'name', name) FROM users"
```

On parse errors, throws `sqldeep::Error` (subclass of `std::runtime_error`)
with `line()` and `col()` accessors.

## Building

```sh
git clone https://github.com/marcelocantos/sqldeep.git
cd sqldeep
mk test     # run tests
mk example  # run the demo
```

Requires C++20 and [mk](https://github.com/marcelocantos/mk).

## Integration

Copy `sqldeep.h` and `sqldeep.cpp` into your project. No external dependencies
at runtime.

If you use an agentic coding tool, include `agents-guide.md` in your project
context.

## License

Apache 2.0. See [LICENSE](LICENSE).
