# Bug: `[SELECT ...]` double-wraps in `json_array(json_group_array(...))`

## Symptom

When `[SELECT ...]` array subquery syntax is used inside an XML/JSONML
attribute, the output is double-wrapped:

```sql
-- input
SELECT xml_to_jsonml(<rc:LineChart data={[SELECT {name: 'a', value: 1}]}/>)

-- actual output
SELECT CAST(xml_element_jsonml('rc:LineChart/',
  xml_attrs_jsonml('data',
    json_array((SELECT json_group_array(json_object('name', 'a', 'value', 1)))))) AS TEXT)

-- expected output
SELECT CAST(xml_element_jsonml('rc:LineChart/',
  xml_attrs_jsonml('data',
    (SELECT json_group_array(json_object('name', 'a', 'value', 1))))) AS TEXT)
```

The result is `[[{"name":"a","value":1}]]` instead of `[{"name":"a","value":1}]`.

## Root cause

`[SELECT ...]` is parsed as an `ArrayLiteral` containing a single element
(the subquery). The renderer emits `json_array(<elements>)` for all array
literals. For `[1, 2, 3]` this is correct. But for `[SELECT expr FROM t]`,
the element is already `(SELECT json_group_array(expr) FROM t)` — wrapping
it in another `json_array(...)` produces a nested array.

## Expected behaviour

`[SELECT ...]` (array literal containing a single SELECT) should emit
just `(SELECT json_group_array(expr) FROM t)` without the outer
`json_array(...)` wrapper.

The distinction:
- `[1, 2, 3]` → `json_array(1, 2, 3)` (literal elements)
- `[SELECT x FROM t]` → `(SELECT json_group_array(x) FROM t)` (subquery aggregation)
- `[1, SELECT x FROM t, 3]` — unclear, probably an error or edge case

## Impact

This blocks the "chart data from SQL" use case. The JSONML chart query:

```sql
SELECT xml_to_jsonml(
  <rc:LineChart data={[SELECT {name, value} FROM (
    SELECT substr(ts,12,5) as name, COUNT(*) as value
    FROM logs GROUP BY 1 ORDER BY 1)]}/>
)
```

Produces `data: [[...]]` instead of `data: [...]`, so Recharts receives
nested arrays and renders nothing.

## Workaround

On the JS side, unwrap single-element arrays:
```javascript
if (Array.isArray(v) && v.length === 1 && Array.isArray(v[0])) v = v[0];
```

But this is fragile and shouldn't be necessary.

## Context

Discovered while building an SRE dashboard demo with sqlpipe. The charts
use `xml_to_jsonml()` to produce Recharts component trees from SQL queries.
The data prop needs to be a flat array of objects — the double-wrapping
breaks it.
