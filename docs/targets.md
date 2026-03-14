# Targets

<!-- last-evaluated: c1e87d9 -->

## Active

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable
- **Weight**: 4 (value 13 / cost 3)
- **Estimated-cost**: 3
- **Acceptance**:
  - All items in STABILITY.md are marked **Stable** (no "Needs review" remaining)
  - Version macros in `sqldeep.h` read `1.0.0`
  - `sqldeep_version()` returns `"1.0.0"`
  - STABILITY.md "Gaps and prerequisites" section is resolved or moved to "Out of scope"
  - GitHub issue #10 is closed (resolved by computed key syntax)
  - Tagged release `v1.0.0` exists on GitHub
- **Context**: All major features (join paths, singular selects, JSON paths, PostgreSQL, FK-guided joins, aggregates, computed keys) have been implemented and tested through v0.6.0 (298 assertions). The remaining work is stabilisation housekeeping, not feature development.
- **Status**: converging
- **Discovered**: 2026-03-12

### 🎯T2 Recursive tree construction from self-referential tables
- **Weight**: 5 (value 13 / cost 3)
- **Estimated-cost**: 3
- **Acceptance**:
  - `SELECT/1 { id, name, children: * } FROM t RECURSE ON (parent_id) WHERE parent_id IS NULL` produces correct nested JSON tree
  - SQLite backend emits bracket-injection CTE (proven approach)
  - PostgreSQL backend emits equivalent
  - Forest output (multiple roots) wrapped in `[]`
  - Singular select (`/1`) returns single root tree
  - Explicit PK supported: `RECURSE ON (parent_id = category_id)`
  - Integration tests verify actual SQLite execution with 3+ level trees
  - Transpilation tests for both backends
- **Context**: Novel feature — no existing query language offers declarative tree construction from flat relational data via comprehension syntax. The `*` marker is a fixed-point annotation; the rewriter emits a mechanical CTE template. Design and SQL template proven working; AST changes started, parser and renderer still needed.
- **Status**: converging
- **Discovered**: 2026-03-12

## Achieved
