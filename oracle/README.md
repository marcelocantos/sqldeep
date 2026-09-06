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

    cv oracle          # both manifests: contract subset (gated) + full tape (advisory)
    cv oracle-study SEED=<hex>

## Two manifests, deliberately

`harness-contract.yaml` is the gated one: the tape omits `CREATE TABLE … AS
<sqldeep>`, and the two transpilers agree **exactly** — zero divergence on
every field at every horizon out to 90 statements, across 200 seeded scripts.
That is the baseline in `baseline-contract.yaml`.

`harness.yaml` runs the wider tape and is **red**. It stays because narrowing a
tape until the number goes green is precisely the Goodhart move this harness
exists to prevent: the narrowed tape lives beside the wide one, never instead
of it.

## The open finding

`CREATE TABLE t AS <sqldeep-select>` is not transpiled at all by the current
pipeline — the sqldeep syntax reaches SQLite verbatim and is rejected.
`CREATE VIEW … AS` and a bare `SELECT` both work, so it is specific to CTAS.
Minimal repro, both binaries reading the same two-line tape:

    IN : CREATE TABLE t1 AS SELECT { customers_id, name } FROM customers
    REF: ok  CREATE TABLE t1 AS SELECT json_object('customers_id', customers_id, 'name', name) FROM customers
    PRT: exec_error  unrecognized token: "{"  CREATE TABLE t1 AS SELECT {customers_id, name} FROM customers

    IN : CREATE TABLE t3 AS FROM customers SELECT { name }
    REF: ok  CREATE TABLE t3 AS SELECT json_object('name', name) FROM customers
    PRT: exec_error  near "FROM": syntax error

There is no golden for CTAS in `testdata/`, so this surface is untested by the
regular suite; whether it is in contract is a product decision. Until that is
settled the wide tape reports it and the contract tape gates.
