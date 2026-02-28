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
