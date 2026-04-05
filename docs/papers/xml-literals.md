# XML Literals in sqldeep

## Motivation

sqldeep already extends SQL with JSON object literals (`{name, qty}` →
`json_object('name', name, 'qty', qty)`). This proposal adds XML literals
to sqldeep, enabling SQL queries that produce well-formed XML/HTML directly.

The driving use case is **reactive UI**: combine sqlpipe's query
subscriptions with sqldeep XML literals so a single SQL expression both
queries data and produces the component tree to render it. The browser
subscribes to the query; when data changes, the subscription fires with
new HTML ready to render. No intermediate translation layer needed.

### Before (separate data query + JS rendering)

```javascript
db.subscribe("SELECT property_address, suburb_name, sold_for FROM auctions JOIN suburbs USING(suburb_id)",
  (result) => {
    container.innerHTML = result.rows.map(r =>
      `<tr><td>${escape(r[0])}</td><td>${escape(r[1])}</td><td>$${escape(r[2])}</td></tr>`
    ).join('');
  });
```

### After (single expression produces the UI)

```sql
SELECT <table class="ui celled table">
  <tr><th>Address</th><th>Suburb</th><th>Sold For</th></tr>
  {SELECT <tr>
    <td>{property_address}</td>
    <td>{suburb_name}</td>
    <td>{'$' || sold_for}</td>
  </tr>
  FROM auctions JOIN suburbs USING(suburb_id)}
</table>
```

The browser just does `element.innerHTML = result`.

### Historical context

This design is directly inspired by the original arr.ai (2016–2017), a
reactive expression engine where arr.ai expressions produced `@xml`-tagged
tuples that a React renderer walked to construct component trees. The
entire AuctionFox property auction demo — cards, tables, maps, charts —
was built this way: a single expression per screen that combined relational
joins with XML/JSX component literals. See
`~/work/bitbucket.org/squz/arr.ai/arrai2/demos/` for the original demo
files.

## Design

### SQLite XML functions

Two custom functions registered via `sqlite3_create_function`:

#### `xml_element(tag, [attrs], ...children)`

Produces `<tag [attrs]>children</tag>`.

- **tag**: TEXT — the element name (e.g. `'div'`, `'ui:Table.Row'`).
- **attrs**: TEXT or NULL — attribute string from `xml_attrs()`, or NULL/omitted
  for no attributes. If the first argument after tag is not from `xml_attrs`,
  it's treated as a child.
- **children**: variadic — any number of child values.
  - TEXT children are XML-escaped (`<>&"` → entities).
  - Children produced by `xml_element` or `xml_attrs` are passed through
    verbatim (already well-formed).
  - NULL children are omitted.
  - Numeric children are converted to text.

Self-closing: if there are no children, produces `<tag [attrs]/>`.

The function must distinguish "raw XML from another xml_element call" from
"plain text that needs escaping". Implementation uses **BLOB type**: all
XML output is returned as `BLOB` via `sqlite3_result_blob()`. Consumers
check `sqlite3_value_type() == SQLITE_BLOB` to detect already-formed XML
and pass it through without escaping. The transpiler emits
`CAST(xml_element(...) AS TEXT)` at the top level and at JSON boundaries
to convert the final result back to a string.

Alternatives considered and rejected:

1. **Sentinel prefix** (`\x01`): Survives all string operations but leaks
   into JSON values and requires manual stripping.
2. **SQLite subtype**: Clean API (`sqlite3_result_subtype()` /
   `sqlite3_value_subtype()`) but subtypes are stripped by `group_concat`
   and other built-in string operations.

#### `xml_attrs(name1, value1, name2, value2, ...)`

Produces ` name1="escaped_value1" name2="escaped_value2"`.

- Takes an even number of arguments (name/value pairs).
- Names are emitted verbatim (must be valid XML attribute names).
- Values are escaped: `"` → `&quot;`, `&` → `&amp;`, `<` → `&lt;`, `>` → `&gt;`.
- NULL values: the attribute is omitted entirely.
- Boolean-like: if the value is the integer 1, emit the attribute name
  alone (e.g. `xml_attrs('celled', 1)` → ` celled`). If 0, omit it.

Returns a BLOB so xml_element knows not to escape it.

### sqldeep transpilation

The sqldeep parser/transpiler gains XML literal support.

#### Static elements

```sql
-- input
<div class="card">hello</div>

-- output
xml_element('div', xml_attrs('class', 'card'), 'hello')
```

#### Dynamic content (expression interpolation)

Curly braces `{expr}` embed SQL expressions inside XML content,
following JSX convention:

```sql
-- input
<td>{property_address}</td>

-- output
xml_element('td', property_address)
```

A literal `{` in XML content is expressed as `{'{'}` (interpolation of a
SQL string literal), mirroring JSX's `{'{'}`. Similarly `}` → `{'}'}`.

#### Dynamic attributes

```sql
-- input
<span style={style_expr}>text</span>

-- output
xml_element('span', xml_attrs('style', style_expr), 'text')
```

#### Nested subqueries

Curly braces containing a SELECT produce aggregated children:

```sql
-- input
<ul>
  {SELECT <li>{name}</li> FROM items WHERE active}
</ul>

-- output
xml_element('ul',
  (SELECT group_concat(
    xml_element('li', name), '')
   FROM items WHERE active))
```

The inner SELECT uses a custom `xml_agg(value)` aggregate that
concatenates XML-typed values (BLOBs) and returns a BLOB. This preserves
the BLOB type through aggregation so xml_element can pass the result
through without escaping.

#### Self-closing elements

```sql
-- input
<img src={url}/>

-- output
xml_element('img', xml_attrs('src', url))
```

(No children → self-closing output.)

#### Namespaced tags

Tags with `:` or `.` are passed through as the tag string. The browser
renderer resolves them (e.g. `ui:Table.Row` → Semantic UI component):

```sql
-- input
<ui:Table.Cell>{value}</ui:Table.Cell>

-- output
xml_element('ui:Table.Cell', value)
```

### Interaction with existing sqldeep features

- **JSON objects inside XML**: `{...}` with field names is a JSON object
  (existing feature). `{expr}` with a single expression is interpolation.
  The parser distinguishes by context: inside XML content, `{expr}` is
  interpolation; in a SELECT list, `{name, qty}` is a JSON object.
  
- **Nesting**: XML can contain `{SELECT ...}` which can contain XML which
  can contain `{SELECT ...}`, etc. Arbitrary depth.

- **XML inside JSON**: XML literals are valid expressions inside JSON object
  fields. The XML transpiles to an `xml_element(...)` call whose TEXT result
  becomes the JSON field value:

  ```sql
  -- input
  SELECT { name, card: <div class="card"><h3>{name}</h3></div> }
  FROM items

  -- output
  SELECT json_object('name', name,
    'card', xml_element('div', xml_attrs('class', 'card'),
              xml_element('h3', name)))
  FROM items
  ```

  This is useful for APIs that return structured data with pre-rendered HTML
  fragments (e.g. a search result with a `snippet_html` field).

- **JSON path navigation inside XML**: The existing `(expr).path[n]` JSON
  extraction syntax works inside XML interpolation:

  ```sql
  -- input
  <td>{(metadata).address.street}</td>
  <span>{(config).themes[0]}</span>

  -- output (SQLite)
  xml_element('td', json_extract(metadata, '$.address.street'))
  xml_element('span', json_extract(config, '$.themes[0]'))
  ```

  No special handling needed — the parser already resolves `(expr).path`
  to `json_extract()` / `jsonb_extract_path()` before the XML transpiler
  sees it.

- **JSON objects inside XML**: Since `{...}` denotes interpolation in XML
  context, embedding a JSON object literal requires double braces —
  `{{name, qty}}` — where the outer braces are interpolation and the inner
  braces are the JSON object:

  ```sql
  -- input
  <td>{{name, qty}}</td>

  -- output
  xml_element('td', json_object('name', name, 'qty', qty))
  ```


### Implementation in sqlpipe

The XML functions are registered on the `sqlite3*` handle during
`Database` construction (alongside `sqlite3_update_hook` etc.). They're
also available in the Wasm build for browser-side use.

sqldeep already auto-transpiles in `Database::exec()`, `Database::query()`,
and `Database::subscribe()`. No additional plumbing needed — XML literals
just work anywhere SQL is accepted.

### Browser rendering

For HTML output, `element.innerHTML = result` is sufficient.

For component rendering (Semantic UI, Chart.js, etc.), a small client-side
translator resolves namespaced tags. Two approaches:

1. **Direct HTML**: sqldeep emits standard HTML tags. `<ui:Table celled>`
   → `<table class="ui celled table">`. The mapping lives in sqldeep.

2. **JSONML hybrid**: For component-heavy UIs, output JSONML instead of
   XML strings (switch `xml_element` to emit `json_array` calls). The
   browser resolves component names via a registry. This is closer to the
   arr.ai model.

Recommendation: Start with HTML output. Add JSONML as an alternative
backend if component resolution proves necessary.

## Example: AuctionFox in sqldeep

```sql
-- Schema
CREATE TABLE suburbs (suburb_id INTEGER PRIMARY KEY, suburb_name TEXT, suburb_image TEXT);
CREATE TABLE properties (property_id INTEGER PRIMARY KEY, suburb_id INTEGER REFERENCES suburbs,
                         property_address TEXT, property_image TEXT);
CREATE TABLE auctions (auction_id INTEGER PRIMARY KEY, property_id INTEGER REFERENCES properties,
                       opening_bid TEXT, sold_for TEXT, bidders INTEGER);

-- Card view (subscribe to this)
SELECT <div class="cards">
  {SELECT <div class="card">
    <img src={property_image}/>
    <div class="card-body">
      <h3>{property_address}</h3>
      <p class="suburb">{suburb_name}</p>
      <p class="price">{'$' || sold_for}</p>
      <p class="details">{bidders || ' bidders'}</p>
    </div>
  </div>
  FROM auctions
  JOIN properties USING(property_id)
  JOIN suburbs USING(suburb_id)}
</div>
```

Output: a single HTML string with all cards, properly escaped, ready
to render. Subscribe to it via sqlpipe; insert a new auction and the
subscription fires with updated HTML.

## Scope

### In scope

- XML literal syntax in sqldeep (`<tag>`, `{expr}`, `{SELECT ...}`)
- `xml_element`, `xml_attrs` SQLite functions (C, registered on handle)
- `xml_agg` aggregate function
- Self-closing elements, namespaced tags, boolean attributes
- Escaping of text content and attribute values

### Out of scope (future)

- JSONML output backend (alternative to XML strings)
- Component resolution in sqldeep (mapping `ui:Table` → HTML)
- Event handler attributes (`onclick`, etc.)
- XML fragments / dangerously-set-inner-html escape hatch
- XML namespaces (xmlns declarations) — tags with `:` are passed through
  as opaque strings, not resolved

## Open questions

1. ~~**Sentinel vs subtype vs xml_agg**~~: Resolved — using BLOB type
   with `xml_agg` aggregate. BLOBs provide type-level distinction without
   sentinel bytes, and `CAST(... AS TEXT)` at boundaries converts cleanly.

2. **Ambiguity with JSON objects**: In `SELECT <td>{x}</td>`, the `{x}`
   is clearly interpolation. But what about `SELECT {x}` — is that a
   JSON object or interpolation? Current rule: JSON object syntax only
   triggers when there are named fields (`{name, qty}`) or explicit
   colon syntax (`{a: expr}`). A bare `{x}` in non-XML context remains
   a JSON object with key "x". Inside XML, it's interpolation.

3. **Whitespace**: Should sqldeep strip insignificant whitespace between
   XML elements, like JSX does? Or preserve it literally?

4. **Top-level XML**: Should `SELECT <div>...</div>` be valid (the entire
   SELECT result is one XML string)? Or must XML only appear as a column
   expression? The former is more natural for the UI-producing use case.
