# sqldeep

Transpiler for JSON5-like SQL syntax to standard SQL with database-specific JSON
functions. Backends: SQLite (default), PostgreSQL, vanilla SQLite.

Agent-facing architecture, build commands, syntax reference, and conventions are
in [CLAUDE.md](CLAUDE.md) — treat it as canonical for this repo. A condensed
integration guide for consumers of the library is
[`dist/sqldeep-agents-guide.md`](dist/sqldeep-agents-guide.md); the public API
stability catalogue is [STABILITY.md](STABILITY.md).
