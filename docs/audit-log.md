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

## 2026-03-15 — /release v0.7.0

- **Commit**: `a57a7fd`
- **Outcome**: Released v0.7.0. Replaced `//` comments with SQL-standard `--` line comments and `/* ... */` block comments (breaking change). Added `->` / `->>` JSON operator passthrough and FROM-context restriction for join arrows. Added per-feature stability levels (Stable/Experimental) to STABILITY.md. Added fixed-point comprehensions research paper. Added RecursiveSelect AST scaffolding. All 312 assertions pass (44 test cases).
- **Deferred**:
  - Bracket-quoted identifiers (`[column name]`) not supported — conflicts with array literal syntax. Documented as known limitation with double-quote workaround.
  - Operator table is a linear scan (info — carried from v0.1.0, still fine for current token set size)

## 2026-03-15 — /release v0.8.0

- **Commit**: `44b26a9`
- **Outcome**: Released v0.8.0. Added recursive tree construction (`RECURSE ON`) with bracket-injection CTE. Fixed Go bindings broken `#include`. Updated README with recursive select docs and comment syntax. All 325 assertions pass (47 test cases).

## 2026-04-05 — /release v0.9.0

- **Commit**: `e7cbb27`
- **Outcome**: Released v0.9.0. Added XML/HTML literal syntax (`<tag attr={expr}>...</tag>` → `xml_element(...)` calls). Added interactive CLI (`sqldeep`) wrapping SQLite shell with transpilation and XML functions. Updated README, agent guide, NOTICES, STABILITY.md. 363 assertions pass (58 test cases).

## 2026-04-05 — /release v0.10.0

- **Commit**: `fe5cdec`
- **Outcome**: Released v0.10.0. Extracted XML SQLite functions (`xml_element`, `xml_attrs`, `xml_agg`) into shared `dist/sqldeep_xml.h`/`.c` with public `sqldeep_register_sqlite_xml()`. Go binding gains `RegisterSQLiteXML()`. Fixed `ar` → `libtool` for macOS archive alignment. 363 assertions pass (58 test cases).

## 2026-04-05 — /release v0.11.0

- **Commit**: `1c02a67`
- **Outcome**: Released v0.11.0. Replaced XML sentinel byte (`\x01`) with BLOB type protocol — XML functions return BLOBs, transpiler emits `CAST(... AS TEXT)` at boundaries. Fixes sentinel leak into JSON values. 362 assertions pass (58 test cases).

## 2026-04-05 — /release v0.12.0

- **Commit**: `4692c1a`
- **Outcome**: Released v0.12.0. Added `xml_to_jsonml()` transpiler macro for JSONML output — emits `xml_element_jsonml`/`xml_attrs_jsonml`/`jsonml_agg` runtime functions that build JSON arrays directly. Updated README, agents guide, CLAUDE.md, STABILITY.md. 382 assertions pass (67 test cases).

## 2026-04-05 — /release v0.13.0

- **Commit**: `f2db49b`
- **Outcome**: Released v0.13.0. Added self-closing void element distinction (`xml_element('br/')` convention) and multi-line XML dedent (common whitespace prefix stripping). Updated README, agents guide, CLAUDE.md, STABILITY.md. 387 assertions pass (69 test cases).

## 2026-04-05 — /release v0.14.0

- **Commit**: `d8fd878`
- **Outcome**: Released v0.14.0 (darwin-arm64, linux-amd64, linux-arm64). Added `jsx()` and `jsonml()` output modes (replacing `xml_to_jsonml()`), JSON boolean auto-wrapping (`true`/`false` → `json('true')`/`json('false')`), boolean attribute semantics via subtype 74, object/array literals at any paren depth, `--help-agent` CLI flag. Added release CI workflow with Homebrew tap integration. 422 assertions pass (79 test cases).

## 2026-04-06 — /release v0.15.0

- **Commit**: `e193771`
- **Outcome**: Released v0.15.0 (darwin-arm64, linux-amd64, linux-arm64). Removed all paren-depth restrictions — deep selects, FROM-first, join paths, XML literals, and SELECT/1 now work at any depth. Per-scope FROM context tracking prevents join arrow leakage across paren boundaries. 435 assertions pass (82 test cases).

## 2026-04-07 — /release v0.19.0

- **Commit**: `cd04c32`
- **Outcome**: Released v0.19.0 (darwin-arm64, linux-amd64, linux-arm64). Renamed `sqldeep_register_sqlite_xml` → `sqldeep_register_sqlite`. Go auto-extension via `sqlite3_auto_extension`. 10 Go smoke tests added. Go API added to STABILITY.md surface catalogue.

## 2026-04-06 — /release v0.18.0

- **Commit**: `e9addbc`
- **Outcome**: Released v0.18.0 (darwin-arm64, linux-amd64, linux-arm64). Added qualified bare fields (`sm.repo` → key `repo`, value `sm.repo`). 447 assertions pass (84 test cases).

## 2026-04-06 — /release v0.17.0

- **Commit**: `ac82ccb`
- **Outcome**: Released v0.17.0 (darwin-arm64, linux-amd64, linux-arm64). Pure BLOB protocol — eliminated all SQLite subtype 74 usage. Added custom JSON functions (`sqldeep_json`, `sqldeep_json_object`, `sqldeep_json_array`, `sqldeep_json_group_array`) that return BLOBs and handle BLOB values natively. Removed `CAST(... AS TEXT)` from all output. `json_extract` wrapped in `CAST((expr) AS TEXT)` for BLOB safety. `SQLITE_SUBTYPE` flag removed from all function registrations. 441 assertions pass (84 test cases).

## 2026-04-06 — /release v0.16.0

- **Commit**: `3f94819`
- **Outcome**: Released v0.16.0 (darwin-arm64, linux-amd64, linux-arm64). JSONML/JSX functions switched from BLOB protocol to TEXT with JSON subtype 74 — no CAST needed, views composable, json_object preserves inline JSON. XML mode retains BLOB+CAST. SQLITE_SUBTYPE flag on all subtype-aware functions. 441 assertions pass (84 test cases).

## 2026-04-07 — /release v0.20.0

- **Commit**: `6c508b9`
- **Outcome**: Released v0.20.0 (darwin-arm64, linux-amd64, linux-arm64). Multi-language bindings: Swift (pure Swift runtime + CSQLDeep transpiler wrapper, macOS SPM + iOS Simulator), Kotlin/Android (JNI transpiler bridge + pure Kotlin runtime, Android device/emulator + JVM desktop). Shared test suite: 79 YAML-driven integration tests exercised across C++, Go, Swift, and Kotlin. README and agents guide updated with binding documentation. 474 assertions pass (8 test cases).

## 2026-04-08 — /release v0.21.0

- **Commit**: `9ac5ded`
- **Outcome**: Released v0.21.0 (darwin-arm64, linux-amd64, linux-arm64). Added SQLDEEP_SQLITE_VANILLA backend for environments without custom functions. Go TranspileVanilla/TranspileFKVanilla convenience functions. 650 assertions pass (10 test cases).
