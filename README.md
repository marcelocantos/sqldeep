# sqldeep

A C++ transpiler that converts JSON5-like SQL syntax into standard SQLite JSON
functions. Write nested JSON queries naturally; sqldeep rewrites them into
`json_object()`, `json_array()`, and `json_group_array()` calls.

## Example

**Input:**

```sql
SELECT {
    id,
    name,
    orders:
        SELECT {
            order_id: id,
            items:
                SELECT {
                    item: 'item-' || name,
                    qty,
                } FROM items i WHERE order_id = o.id,
        } FROM orders o WHERE customer_id = p.id,
} FROM people p;
```

**Output:**

```sql
SELECT json_object('id', id, 'name', name, 'orders',
  (SELECT json_group_array(json_object('order_id', id, 'items',
    (SELECT json_group_array(json_object('item', 'item-' || name, 'qty', qty))
     FROM items i WHERE order_id = o.id)))
   FROM orders o WHERE customer_id = p.id))
FROM people p;
```

## Syntax

### Object literals

Use `{ }` after `SELECT` to construct JSON objects:

```sql
SELECT { id, name } FROM users
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

## License

Apache 2.0. See [LICENSE](LICENSE).
