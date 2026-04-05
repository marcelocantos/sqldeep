# Targets

<!-- last-evaluated: 149089b -->

## Active

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable
- **Weight**: 4 (value 13 / cost 3)
- **Estimated-cost**: 3
- **Acceptance**:
  - All items in STABILITY.md are marked **Stable** (no Experimental or Needs review remaining)
  - STABILITY.md "Gaps and prerequisites" section is resolved or moved to "Out of scope"
  - Settling threshold met (N consecutive minor releases with zero breaking changes)
  - Version macros in `sqldeep.h` read `1.0.0`
  - `sqldeep_version()` returns `"1.0.0"`
  - GitHub issue #10 is closed (resolved by computed key syntax)
  - Tagged release `v1.0.0` exists on GitHub
- **Context**: v0.7.0 introduced Experimental items (SQL comments, `->` FROM-context restriction, `->>` passthrough) and a breaking change (removed `//` comments). v0.8.0 adds recursive select (also Experimental). The settling clock requires these features to prove out through real-world usage before promotion to Stable.
- **Status**: blocked (settling period — need Experimental items to prove out)
- **Discovered**: 2026-03-12

### 🎯T3 XML/HTML literals produce well-formed markup directly from SQL queries
- **Weight**: TBD
- **Acceptance**:
  - `<tag attr={expr}>...</tag>` transpiles to `xml_element('tag', xml_attrs('attr', expr), ...)` calls
  - `{expr}` interpolation inside XML content and attributes
  - `{SELECT ...}` nested subqueries inside XML with aggregation (`xml_agg` or sentinel)
  - Self-closing elements, namespaced tags, boolean attributes
  - XML literals valid inside JSON object fields (`{ name, card: <div>...</div> }`)
  - JSON path navigation `(expr).path[n]` works inside XML interpolation
  - `{{name, qty}}` produces JSON object inside XML (outer = interpolation, inner = object)
  - Literal braces via `{'{'}` / `{'}'}`
  - Both SQLite and PostgreSQL backends
  - Transpilation tests and SQLite integration tests
  - XML functions registered in sqlpipe (out of scope for sqldeep itself — sqldeep is string→string)
- **Context**: Design paper at `docs/papers/xml-literals.md`. Motivated by sqlpipe reactive UI — single SQL expression produces query + component tree. Inspired by arr.ai AuctionFox demo.
- **Status**: achieved (v0.9.0)
- **Discovered**: 2026-04-05

## Achieved

### 🎯T2 Recursive tree construction from self-referential tables
- **Achieved**: 2026-03-15 (commit 149089b)
- **Acceptance**: All criteria met — parser, renderer, both backends, singular/forest modes, explicit PK, integration tests with 3+ levels, transpilation tests. 47 tests, 325 assertions passing.
- **Discovered**: 2026-03-12
