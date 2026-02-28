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
SQLDEEP_VERSION       // "0.1.0"
SQLDEEP_VERSION_MAJOR // 0
SQLDEEP_VERSION_MINOR // 1
SQLDEEP_VERSION_PATCH // 0
```

## Syntax quick reference

| Input pattern | Output |
|---|---|
| `SELECT { fields } FROM ...` | `SELECT json_object(...) FROM ...` |
| Nested `SELECT { ... } FROM ...` | `(SELECT json_group_array(json_object(...)) FROM ...)` |
| `SELECT [expr] FROM ...` | `SELECT json_group_array(expr) FROM ...` |
| `[expr, ...]` | `json_array(...)` |
| `{ fields }` (inline) | `json_object(...)` |

### Field forms

| Form | Example | Emits |
|---|---|---|
| Bare | `id,` | `'id', id` |
| Renamed | `order_id: id` | `'order_id', id` |
| Quoted key | `"order id": id` | `'order id', id` |

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
- **Subquery wrapping.** `(SELECT { ... })` in parentheses is recognised
  specially — sqldeep avoids double-wrapping with extra parens.

## Common patterns

### Basic object query

```
SELECT { id, name, email } FROM users WHERE active = 1
```

### Nested one-to-many

```
SELECT {
    id,
    name,
    orders: SELECT { order_id: id, total } FROM orders o WHERE o.cid = c.id,
} FROM customers c
```

### Three-level nesting

```
SELECT {
    id,
    orders: SELECT {
        order_id: id,
        items: SELECT { name, qty } FROM items i WHERE i.oid = o.id,
    } FROM orders o WHERE o.cid = c.id,
} FROM customers c
```

### Inline array

```
SELECT { tags: [1, 'two', 3] } FROM items
```
