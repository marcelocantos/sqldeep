# sqldeep transpiler-parity oracle

A [differential-harness](https://github.com/marcelocantos/differential-harness)
instance (`~/think/differential-harness`). Two independently written
transpilers are driven through identical seeded SQL scripts against identical
in-memory SQLite sessions, and their result rows are diffed tick by tick.

| side | implementation |
|---|---|
| reference | `oracle/ref/sqldeep.cpp` — the frozen v0.21.0 hand-written transpiler (own lexer, parser, renderer) |
| port | `dist/sqldeep.cpp` — the current deepparser AST-rewrite pipeline |

Both link their own copy of the transpiler into their own binary; nothing is
shared but the tape and vendored SQLite.

## Running

    cv oracle          # both manifests: contract subset + full tape (both gated)
    cv oracle-study SEED=<hex>

## Two manifests, both gated

`harness-contract.yaml` runs the narrow tape: it omits `CREATE TABLE …  AS
<sqldeep>`. `harness.yaml` runs the wide tape, which includes it. Both are
gated by `cv oracle`, and both are green — zero divergence on every field at
every horizon out to 90 statements, across 200 seeded scripts. Baselines:
`baseline-contract.yaml` and `baseline.yaml`.

The narrow tape stays because it is the useful bisecting subset, not because
it makes a number go green: narrowing a tape until acceptance passes is
precisely the Goodhart move this harness exists to prevent, so the narrowed
tape lives beside the wide one, never instead of it.

## The finding this harness caught (closed)

`CREATE TABLE t AS <sqldeep-select>` was not transpiled at all: the
`Transformer` walked `LP_STMT_CREATE_VIEW`'s child select but had no case for
`LP_STMT_CREATE_TABLE`, so the sqldeep syntax reached SQLite verbatim and was
rejected. `CREATE VIEW … AS`, `INSERT … SELECT` and a bare `SELECT` all worked,
which is why nothing else saw it. The wide tape was red on it from the day it
was written and is green now that `dist/sqldeep.cpp` walks
`create_table.as_select`; `testdata/transpile.yaml` and `testdata/sqlite.yaml`
carry the goldens.
