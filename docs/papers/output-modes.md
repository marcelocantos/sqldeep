# XML Output Modes

## Motivation

sqldeep's XML literals currently support two output formats:

1. **XML strings** (default): `<div class="card">hello</div>` emits
   `xml_element('div', xml_attrs('class', 'card'), 'hello')`, producing
   HTML text.

2. **JSONML**: `xml_to_jsonml(<div class="card">hello</div>)` emits
   `xml_element_jsonml(...)`, producing `["div",{"class":"card"},"hello"]`.

Both modes stringify attribute values. In JSONML mode, a dynamic attribute
like `data={json_object('x', 1)}` produces `{"data":"{\"x\":1}"}` — the
JSON object is serialised to a string, then the string is JSON-escaped
into the attribute. The renderer must `JSON.parse` it back.

This is fine for XML/HTML (attributes are always strings), but for
component-based UIs that consume JSONML, it's a lossy round-trip. A
React component receiving `data` as a prop expects a JavaScript object,
not a JSON-encoded string.

### The JSX model

In JSX, `<Graph data={{x: 1, y: 2}}/>` passes the object directly as a
prop — no serialisation. The attribute value is a live JavaScript value,
not a string.

sqldeep can support the same semantics in its JSONML output: when an
attribute value is already valid JSON, emit it as a JSON value in the
JSONML attributes object rather than wrapping it in quotes.

## Design

### Three output modes via pseudo-function syntax

| Syntax | Transpiles to | Attribute values |
|---|---|---|
| `<div>...</div>` | `xml_element(...)` | Always strings (XML semantics) |
| `jsonml(<div>...</div>)` | `xml_element_jsonml(...)` | Always strings |
| `jsx(<div>...</div>)` | `xml_element_jsx(...)` | JSON-valued attrs preserved |

`xml_to_jsonml(...)` remains as a deprecated alias for `jsonml(...)`.

All three forms use the same XML literal parser. The mode only affects
code generation and runtime function selection.

### Parsing

The transpiler already pattern-matches `xml_to_jsonml(` as a special form
(a pseudo-function wrapping an XML literal). `jsonml(` and `jsx(` use the
same mechanism:

```
parse_sql_parts:
  if token is Ident and text in {"jsonml", "jsx", "xml_to_jsonml"}:
    if next is '(' and then '<' Ident:
      parse_xml_element(depth, mode)
      expect ')'
```

Bare `<div>` (no wrapper) continues to default to XML mode.

### JSX attribute semantics

In `jsx(...)` mode, dynamic attributes (`name={expr}`) are marked so the
runtime can preserve JSON values:

```sql
-- input
jsx(<Graph data={(SELECT {x, y} FROM points)} label="Sales"/>)

-- output
xml_element_jsx('Graph',
  xml_attrs_jsx('data', (SELECT json_group_array(
    json_object('x', x, 'y', y)) FROM points),
  'label', 'Sales'),
)
```

The `xml_attrs_jsx` runtime function differs from `xml_attrs_jsonml` in
one way: for each value, it checks whether the value is already valid
JSON (object or array). If so, it emits the value as raw JSON in the
attributes object. If not, it emits it as a JSON string.

```
xml_attrs_jsonml('data', '[{"x":1}]', 'label', 'Sales')
→ {"data":"[{\"x\":1}]","label":"Sales"}     -- data is a string

xml_attrs_jsx('data', '[{"x":1}]', 'label', 'Sales')
→ {"data":[{"x":1}],"label":"Sales"}         -- data is parsed JSON
```

### Detection strategy

The runtime needs to distinguish "this value is JSON, pass it through"
from "this value is a plain string, quote it". Two options:

**Option A: Value sniffing** — `xml_attrs_jsx` inspects each value. If
it starts with `{` or `[` and is valid JSON, emit raw. Otherwise quote.

- Pro: No transpiler changes beyond function selection.
- Con: A string that happens to look like JSON (e.g. a user-entered
  `[1,2,3]`) gets unintentionally promoted. Fragile.

**Option B: Type tagging** — The transpiler marks dynamic attributes by
passing values through a sentinel function or using SQLite's subtype
mechanism. Only values explicitly produced by JSON-emitting expressions
(`json_object`, `json_group_array`, `{...}` literals, `[...]` literals)
are promoted.

- Pro: No false positives. The transpiler knows which expressions
  produce JSON.
- Con: Requires subtype propagation or a wrapper function. Subtypes
  are stripped by many SQLite builtins (including `group_concat`), so
  a marker function like `json_value(expr)` that sets a subtype may
  not survive aggregation.

**Option C: Positional tagging** — `xml_attrs_jsx` receives an
additional bitmask or flag argument indicating which attribute positions
are dynamic. The transpiler generates:

```sql
xml_attrs_jsx('data', (SELECT ...), 'label', 'Sales', 1)
--                     ^dynamic      ^static          ^mask: bit 0 = dynamic
```

- Pro: No runtime inspection, no subtype fragility. The transpiler
  already tracks `is_dynamic` per attribute.
- Con: Slightly more complex calling convention.

**Recommendation: Option A with a refinement.** Rather than sniffing
the value text, use SQLite's `json_valid()` function. The transpiler
emits `xml_attrs_jsx` only in JSX mode; the function calls
`json_valid(value)` on each dynamic attribute value. If valid JSON
object or array, emit raw. If scalar or invalid, emit as string.

This is safe because in JSX mode the user has explicitly opted into
"preserve structured data". A string that happens to parse as JSON is
rare in practice, and JSX mode is not the default. If a user truly
wants a string `[1,2,3]` in JSX mode, they can wrap it in quotes in
the source.

### Runtime functions

New functions for JSX mode:

#### `xml_element_jsx(tag, [attrs], ...children)`

Identical to `xml_element_jsonml`. The element structure is the same;
the difference is only in attribute handling, which is in `xml_attrs_jsx`.

Could alias to `xml_element_jsonml` directly, but a separate name keeps
the functions self-documenting and allows future divergence (e.g. if JSX
mode gains support for `children` as structured data too).

#### `xml_attrs_jsx(name1, value1, name2, value2, ...)`

Like `xml_attrs_jsonml`, but for each value:
1. If the value is a BLOB (already-formed XML/JSONML), pass through raw.
2. If the value is TEXT and `json_valid(value)` returns true and the
   first character is `{` or `[`, emit the value as raw JSON (no quoting).
3. Otherwise, emit as a JSON string (`"value"`).

#### `jsx_agg(value)`

Identical to `jsonml_agg`. Could alias.

### Transpiler changes

In `render_xml_element`, when the element's mode is JSX:

```cpp
const char* fn_element = "xml_element_jsx('";
const char* fn_attrs   = ", xml_attrs_jsx(";
```

The `DeepSelect` wrapper uses `jsx_agg` instead of `jsonml_agg` or
`xml_agg`.

### Example

```sql
-- Input
jsx(<Graph
  data={(SELECT {x, y} FROM points)}
  title="Revenue"
  config={{color: 'blue', animated: true}}
/>)

-- Transpiled SQL
xml_element_jsx('Graph/',
  xml_attrs_jsx(
    'data', (SELECT json_group_array(
      json_object('x', x, 'y', y)) FROM points),
    'title', 'Revenue',
    'config', json_object('color', 'blue', 'animated', 1)
  )
)

-- Runtime output
["Graph",{
  "data":[{"x":1,"y":2},{"x":3,"y":4}],
  "title":"Revenue",
  "config":{"color":"blue","animated":true}
}]
```

The React renderer receives `data` as a parsed array and `config` as a
parsed object. No `JSON.parse` needed.

### Interaction with existing features

- **`jsonml(...)` and `xml_to_jsonml(...)`** are unaffected. They continue
  to stringify all attribute values.

- **Bare `<div>`** remains XML mode. No change.

- **Nesting modes**: A `jsx(...)` subtree can contain bare `<div>` children
  that are also rendered in JSX mode (the mode propagates down, as JSONML
  does today). Mixing modes within a tree is not supported — the outermost
  wrapper wins.

- **`{expr}` in attributes**: Already parsed as dynamic attributes. The
  only change is that JSX mode uses `xml_attrs_jsx` which preserves JSON.

- **`{{key, val}}` in content**: JSON objects in element bodies. In JSX
  mode, these are emitted as raw JSON children (not stringified). This
  may require `xml_element_jsx` to also sniff child values, but that's a
  future extension — start with attribute-only preservation.

## Migration

`xml_to_jsonml(...)` continues to work. It can be deprecated in docs
and eventually removed (e.g. at v1.0).

## Scope

### In scope

- `jsx(...)` and `jsonml(...)` pseudo-function syntax
- `xml_element_jsx`, `xml_attrs_jsx`, `jsx_agg` runtime functions
- JSON-preserving attribute values in JSX mode
- Deprecation of `xml_to_jsonml`

### Out of scope

- JSX-mode children as structured data (future)
- Mixing output modes within a single tree
- Component resolution (mapping tag names to renderers)
- Event handler attributes
- Spread attributes (`{...props}`)

## Open questions

1. **Should `jsonml(...)` also preserve JSON attributes?** Keeping JSONML
   as string-only provides a clean distinction: `jsonml` = structural
   format with string values (safe for serialisation), `jsx` = structural
   format with live values (for component rendering). But the distinction
   may be too subtle — users might expect `jsonml` to do what `jsx` does.

2. **Aliasing vs separate functions**: `xml_element_jsx` could just be
   `xml_element_jsonml` (they're structurally identical). Using a separate
   name is clearer but adds surface area. Same for `jsx_agg` vs
   `jsonml_agg`.

3. **Static attributes in JSX mode**: Should `title="Revenue"` be emitted
   as a string (always) or should JSX mode also try to parse static
   attribute values as JSON? Recommendation: no — static attributes are
   always strings. Only dynamic `{expr}` attributes get JSON preservation.
   This matches JSX semantics: `title="foo"` is always a string, `data={x}`
   depends on the expression.
