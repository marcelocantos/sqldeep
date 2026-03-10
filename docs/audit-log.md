# Audit Log

Chronological record of audits, releases, documentation passes, and other
maintenance activities. Append-only — newest entries at the bottom.

## 2026-02-28 — /open-source v0.1.0

- **Commit**: `584e925`
- **Outcome**: Open-sourced sqldeep as Apache 2.0. Audit found and fixed 5 parser issues (key injection, recursion depth, quote unescaping, unmatched parens, doubled quotes). Added NOTICES + vendor LICENSE files. README, LICENSE, and .gitignore already in good shape. Published to github.com/marcelocantos/sqldeep and released v0.1.0. All 48 tests pass.
- **Deferred**:
  - Number lexing accepts partial floats like `1.` (low — cosmetic, not a security issue)
  - Operator table is a linear scan (info — fine for current token set size)
  - `assert()` used in a few places instead of throwing (info — only triggers on internal logic errors)

## 2026-02-28 — /release v0.2.0

- **Commit**: `51f6359`
- **Outcome**: Released v0.2.0. Added auto-join syntax (`c->orders`) with lexer pre-scan for alias resolution, agents-guide.md, STABILITY.md. All 55 tests pass (7 new: 5 transpilation, 2 SQLite integration).

## 2026-03-01 — /release v0.3.0

- **Commit**: `18cccc4`
- **Outcome**: Released v0.3.0. Added reverse join (`<-`), join path chains, bridge joins, FROM-first syntax for deep and plain selects, GitHub Actions CI (Linux + macOS). All 94 tests pass (39 new since v0.2.0).

## 2026-03-02 — /release v0.4.0

- **Commit**: `82735e0`
- **Outcome**: Released v0.4.0. Added singular select (`SELECT/1`) for single-row projections and FK-guided join path transpilation (explicit FK metadata, multi-column FKs). All 111 tests pass (17 new since v0.3.0).

## 2026-03-04 — /release v0.5.0

- **Commit**: `5db977c`
- **Outcome**: Released v0.5.0. Added PostgreSQL backend support (`jsonb_build_object`, `jsonb_build_array`, `jsonb_agg`, `jsonb_extract_path`), ON/USING override clauses for join paths, JSON path extraction (`(expr).path`), shared YAML test data, Go bindings, and moved dist files to `dist/`. Updated agent guide with ON/USING and JSON path docs. All 260 assertions pass (36 test cases).

## 2026-03-11 — /release v0.6.0

- **Commit**: `b84c667`
- **Outcome**: Released v0.6.0. Added aggregate field syntax (`field: SELECT expr` / `SELECT/1 expr` inside object literals for GROUP BY projections), computed keys (`(expr): value` for dynamic JSON key names). Fixed partial float lexing and replaced `assert()` with proper error reporting. All 298 assertions pass (42 test cases).
