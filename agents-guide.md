# sqldeep — Agent Guide

Transpiler: JSON5-like SQL syntax → standard SQLite JSON functions.
Two-file C++20 library with zero runtime dependencies.

## Integration

Copy `sqldeep.h` and `sqldeep.cpp` into your project. No link-time
dependencies — SQLite is only needed at runtime by the *caller*, not by
sqldeep itself.

## API

```cpp
#include "sqldeep.h"

// Transpile sqldeep syntax to standard SQL.
// Returns the transpiled SQL string.
// Throws sqldeep::Error on parse failure.
std::string sql = sqldeep::transpile(input);
```

### Error handling

`sqldeep::Error` extends `std::runtime_error` and adds source position:

```cpp
try {
    auto sql = sqldeep::transpile(input);
} catch (const sqldeep::Error& e) {
    // e.what() — error message
    // e.line() — 1-based line number
    // e.col()  — 1-based column number
}
```

### Version macros

```cpp
SQLDEEP_VERSION       // "0.3.0"
SQLDEEP_VERSION_MAJOR // 0
SQLDEEP_VERSION_MINOR // 3
SQLDEEP_VERSION_PATCH // 0
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
| `FROM ... SELECT expr` | `SELECT expr FROM ...` (plain rearrangement) |
| `[expr, ...]` | `json_array(...)` |
| `{ fields }` (inline) | `json_object(...)` |

### Field forms

| Form | Example | Emits |
|---|---|---|
| Bare | `id,` | `'id', id` |
| Renamed | `order_id: id` | `'order_id', id` |
| Quoted key | `"order id": id` | `'order id', id` |

- Join paths:
  - `->` (one-to-many): `FROM c->orders o` → `FROM orders o WHERE o.customers_id = c.customers_id`
  - `<-` (many-to-one): `FROM o<-customers c` → `FROM customers c WHERE o.customers_id = c.customers_id`
  - Chains: `FROM c->orders o->items i` → `FROM orders o JOIN items i ON i.orders_id = o.orders_id WHERE o.customers_id = c.customers_id`
  - Bridge (many-to-many): `FROM c->custacct<-accounts a` → `FROM custacct JOIN accounts a ON custacct.accounts_id = a.accounts_id WHERE custacct.customers_id = c.customers_id`
  - Convention: PK = `<table>_id`, FK = same column name in child/parent table.
- `//` line comments are stripped.
- Trailing commas are allowed in objects and arrays.
- SQL without `{ }` or `[ ]` passes through unchanged.

## Gotchas

- **C++20 required.** Uses `std::variant`, structured bindings, and other
  C++20 features.
- **No SQLite dependency.** sqldeep is a pure string→string transform. The
  caller is responsible for executing the output SQL against SQLite.
- **Nesting depth limit.** Constructs nested beyond 200 levels throw
  `sqldeep::Error`.
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

### SELECT-first (also supported)

```
SELECT { id, name } FROM users
SELECT { id, orders: SELECT { total } FROM orders WHERE cid = c.id } FROM customers c
```
