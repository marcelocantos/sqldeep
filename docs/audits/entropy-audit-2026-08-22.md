# Entropy audit — sqldeep — 2026-08-22

## Executive summary

- **Snapshot:** `/Users/marcelo/work/github.com/marcelocantos/sqldeep`, branch `master`, HEAD `bd9257dff0e0a1d7cae168b8a5118cfa83328a94` (`bd9257d Update bullseye.yaml (#25)`), tracking `origin/master` with no ahead/behind.
- **Initial dirty state (before this report):** `?? docs/audit/` only (untracked Fable report at `docs/audit/fable-2026-07.md`). No staged files. That tree was left untouched.
- **Date:** 2026-08-22. Tagged release on HEAD lineage: `v0.23.0`. `SQLDEEP_VERSION` in `dist/sqldeep.h` is `"0.23.0"`.
- **Scope:** in-repo production, tests, bindings, CI, docs, and build. Exclusions below.
- **Headline mechanism:** a clean AST-rewrite core (`lp_parse_all` → `Transformer` → `lp_ast_to_sql`) is boxed in by two unfinished seams. (1) The RECURSE rewriter and the C SQLite runtime still leave the trusted AST/unparser — string-splicing identifiers, a fail-open bare-expression unwrap, and unguarded `strlen`/`malloc` in `sqldeep_xml.c`. (2) The deepparser migration updated the core and `cvfile`, but the documented two-file embed contract, Kotlin CMake, Go/Swift link lines, and CI matrix were not moved with it.
- **Highest-consequence findings:** ENT-001 (NULL tag/key crashes the C runtime, exit 139), ENT-002 (RECURSE identifier splicing yields attacker-chosen SQL), ENT-003 (bare expressions return empty/stripped SQL as success), ENT-007 (embed/bindings still assume a two-file library).
- **Unverified residue:** FO2 realloc-failure / int-overflow on aggregates; FO4 C++ exceptions escaping `extern "C"`; FO5 alias-map leakage across statements; FO6 `int` escaped-length overflow on ~GB inputs; live PostgreSQL execution; Swift XCTest and Kotlin/Android suites (not run this session). Owner-intent questions are in the residue section.

This is the first full entropy-audit report in `docs/audits/`. The untracked Fable-5 report (`docs/audit/fable-2026-07.md`) is prior security evidence; every confirmed Fable finding cited here was revalidated against current source and, where cheap, against the shipped CLI/`libsqldeep.a`.

## Scope and exclusions

**In scope:** `dist/`, `cmd/`, `tests/`, `testdata/`, `examples/`, `go/sqldeep/`, `swift/Sources/` and `swift/Tests/` (source only), `kotlin/src/` and `kotlin/desktop_test.kt`, `cvfile`, `.github/workflows/`, `STABILITY.md`, `README.md`, `CLAUDE.md`, `bullseye.yaml`, `docs/*.md` (except papers as design history).

**Named exclusions (not silent omissions):**

| Tree | Role | Treatment |
|---|---|---|
| `vendor/deepparser/` | git submodule, sqldeep-grammar fork of liteparser | architecture neighbour; not audited as this repo's code |
| `vendor/include/`, `vendor/src/sqlite3.c`, `vendor/src/shell.c` | vendored SQLite / doctest / fkYAML | SQLite `SQLDEEP_SHELL` hook in `shell.c` is in scope; the rest is third-party |
| `build/` | cv output (`*.o`, `libsqldeep.a`, `sqldeep`, `sqldeep_tests`) | gitignored; used only as a shipped-path binary for reproduction |
| `kotlin/build/`, `kotlin/.cxx/`, `swift/.build/` | local binding build caches | gitignored; not read as source |
| `docs/papers/` | design notes | not treated as current architecture law |
| `docs/audit/fable-2026-07.md` | untracked prior audit | evidence source; not modified |

No analyzers were installed. Clone detection, coverage, and ASan-on-sqlite3 were not available as configured project tools.

## Commands run

| Command | Version / notes | Exit | Shipped vs auxiliary | Limitation |
|---|---|---|---|---|
| `git rev-parse --abbrev-ref HEAD`; `git rev-parse HEAD`; `git status --porcelain=v1 -b` | git | 0 | provenance | — |
| `cv test` | `cv v0.10.0` (`/opt/homebrew/bin/cv`); `clang` Apple 21 / Homebrew 22 | 0 | **shipped** C++ tests | 11 cases, 696 assertions, all pass. Does not build CLI, Go, Swift, or Kotlin. |
| `python3` YAML load of `testdata/{sqlite,transpile}.yaml` | system Python | 0 | auxiliary inventory | sqlite.yaml: 79 cases. transpile.yaml: convention 183 (110 with `expected_postgres`), fk 5 (5 PG), errors 23, fk_errors 2. |
| `diff -q dist/sqldeep.h swift/Sources/CSQLDeep/include/sqldeep.h` | — | 0 (identical) | auxiliary | — |
| `diff -q testdata/sqlite.yaml swift/Tests/SQLDeepRuntimeTests/sqlite.yaml` and `kotlin/src/androidTest/assets/sqlite.yaml` | — | 0 (identical today) | auxiliary | No CI check that they stay identical. |
| `c++ -std=c++20 -c dist/sqldeep.cpp -Idist -Ivendor/include` (Kotlin CMake include set) | Apple clang 21 | compile error: `'liteparser.h' file not found` | auxiliary, same flags as `kotlin/src/main/jni/CMakeLists.txt` | Confirms Kotlin native compile is broken without deepparser includes/objects. |
| Tiny C driver linked to `build/libsqldeep.a` (`sqldeep_transpile`) | shipped library | 0 / 139 | **shipped C API** | F1/F3/FO3 reproductions below. |
| `build/sqldeep :memory:` CLI | sqldeep 0.23.0 / SQLite 3.48.0 | 0 / 139 / 133 | **shipped CLI** | F2 SIGSEGV 139; FO1 abort 133 on quoted attrs; F4 malformed JSON exit 0. |
| `GOWORK=off go test .` in `go/sqldeep` | `go version go1.26.4 darwin/arm64` | 0 (`ok … 0.378s`) | auxiliary | Passes only because `build/libsqldeep.a` and `build/sqldeep_xml.o` already exist. Default `go test` failed: parent `~/work/github.com/marcelocantos/go.work` does not list this module. |
| `/Users/marcelo/.claude/skills/hygiene/hygiene_check.py` from repo root | uv-run script | 1 | hygiene validator | `FileNotFoundError: …/hygiene.yaml`. No `hygiene.yaml` in the repo. |
| `git log --name-only -50` churn | — | 0 | history | Top files: `STABILITY.md` (16), `dist/sqldeep.cpp` (15), `testdata/transpile.yaml` (13), `tests/test_sqlite.cpp` (12). |
| `git submodule status` | deepparser `cb3c13bcb7579a7d947295a00f7f99deed1926cc` on branch `sqldeep-grammar` | 0 | — | Submodule not re-audited. |

Not run: Swift XCTest, Kotlin desktop/Android, `cv shell` rebuild, ASan linked with sqlite3, live PostgreSQL, `go test` without a prior `cv` build (would fail the cgo link).

## Dimension vector

| Dimension | State | Evidence summary | Change from baseline |
|---|---|---|---|
| Architecture topology | concern | Core pipeline matches CLAUDE.md (deepparser parse → in-place `Transformer` → unparse). Declared two-file embed and Kotlin/Go/Swift packaging still describe the pre-deepparser library. | n/a (first full audit) |
| Redundancy / sources of truth | concern | C runtime is the live SQLite implementation for C++/Go/Kotlin; Swift reimplements it. `sqlite.yaml` copied twice. Version macros in `STABILITY.md` still say 0.21.0. Agent guide claims zero link-time deps. | n/a |
| Change amplification | concern | Runtime protocol or YAML case changes must be repeated across C + Swift (+ two yaml copies + STABILITY/README/agent-guide). `dist/sqldeep.cpp` is the high-churn hub (15/50 commits). | n/a |
| Local code quality | concern | Transformer is a coherent ~1800-line AST mutator. RECURSE is a text template; XML runtime uses unchecked `strlen`/`realloc` and mismatched length math. | n/a |
| Correctness / verification | concern | Shipped `cv test` is green (696/696). Known crash, injection, fail-open, and malformed-JSON paths have no golden cases. Binding suites exist but are not CI. PostgreSQL is string-compare only. | n/a |
| Security / dependencies | concern | F1/F2/FO1 are live on the shipped CLI and `libsqldeep.a`. Go `init()` auto-registers the crashing runtime on every connection. No dependabot, CodeQL, or sanitizer job. Vendoring is otherwise disciplined. | n/a |
| Build / release / operations | concern | CI is `cv test` on ubuntu-latest + macos-latest. Release builds the CLI tarball only. Go module tags exist through `go/sqldeep/v0.23.0` but cgo links gitignored `build/`. Kotlin gradle wrapper is gitignored. | n/a |
| Documentation / governance | concern | CLAUDE.md architecture is accurate; README/STABILITY/agent-guide embed contract is not. `docs/TODO.md` is a completed leftover. Fable report cited by 🎯T7 is untracked. No `AGENTS.md`, no `hygiene.yaml`, no CODEOWNERS. | n/a |

Do not collapse this vector to a score.

## Observed architecture

```
                    sqldeep_transpile_*  (C API, dist/sqldeep.h)
                              │
                              ▼
                     transpile_impl (dist/sqldeep.cpp)
                              │
              ┌───────────────┼────────────────┐
              ▼               ▼                ▼
     lp_parse_all      Transformer       lp_ast_to_sql
     (deepparser)    post-order rewrite   (deepparser)
                              │
                              ▼
                    standard SQL text
                              │
         ┌────────────────────┼─────────────────────┐
         ▼                    ▼                     ▼
   SQLite runtime      PostgreSQL jsonb        SQLite vanilla
   sqldeep_json_*      (transpile only)        json_object / …
   xml_* (C or Swift)
```

**Entry points**

- C API: `sqldeep_transpile{,_fk,_backend,_fk_backend}`, `sqldeep_version`, `sqldeep_free` (`dist/sqldeep.h`).
- SQLite runtime registration: `sqldeep_register_sqlite` (`dist/sqldeep_xml.h` / `.c`).
- CLI: `cmd/sqldeep.c` `#include`s vendored `shell.c` with `SQLDEEP_SHELL`; every statement is transpiled first.
- Go: `go/sqldeep` cgo wrapper; `init()` → `sqldeep_enable_auto` → `sqlite3_auto_extension`.
- Swift: `CSQLDeep` clang module + `SQLDeepRuntime.swift` (pure Swift runtime) + `Transpiler.swift`.
- Kotlin: JNI (`sqldeep_jni.c`) + CMake shared library.

**Declared rules that agree with the code**

- Sqldeep is an AST-to-AST mutator; deepparser owns parse and print (`CLAUDE.md` pipeline; `dist/sqldeep.cpp` header comment; `cvfile` bundles `dp_objs` into `libsqldeep.a`).
- Public C surface is the small header in `dist/sqldeep.h`.
- YAML in `testdata/` drives C++ doctest and (for transpile + sqlite) the Go tests.
- PostgreSQL is a function-name backend, not a second parser (`STABILITY.md` v0.22.0 note).

**Observed rules inferred from code (not fully documented as embed law)**

- Every consumer of `sqldeep.cpp` must compile/link `vendor/deepparser/src/{arena,liteparser,lp_tokenize,lp_unparse,parse}.c`.
- Go and Swift do not compile `sqldeep.cpp`; they link `build/libsqldeep.a` produced by `cv`.
- Go and Kotlin execute the C XML/JSON runtime. Swift does not.

**Contradictions**

- README, `STABILITY.md` “Distribution”, and `dist/sqldeep-agents-guide.md` still say copy `sqldeep.h` + `sqldeep.cpp` with no link-time dependencies.
- README (`:380-381`) still claims a pure-Go runtime port; `STABILITY.md:110-111,307` and README (`:422`) still claim a pure-Kotlin port. Go has no `xml_element` implementation; Kotlin `SQLDeepRuntime.kt` is a JNI `nativeRegister` wrapper around `sqldeep_xml.c`.
- 🎯T5 acceptance still says the hand-written parser remains for PostgreSQL; `STABILITY.md` and `transpile_impl` send every backend through deepparser.
- 🎯T7 cites `docs/audit/fable-2026-07.md`, which is untracked.

**Unknown intent (owner judgment)** — see residue.

## Findings

### ENT-001: C XML/JSON runtime `strlen` on SQL NULL crashes the host

- **Priority:** P0
- **Dimensions:** Correctness / verification; Security / dependencies; Local code quality
- **Status:** observed fact
- **Evidence:** `dist/sqldeep_xml.c:119-120` (`sd_xml_element`: `sqlite3_value_text` then `strlen(tag)` with no NULL check). Same unguarded `sqlite3_value_text` + `strlen` on name slots at `:64`, `:68`. Shipped CLI: `SELECT xml_element(NULL);` → exit **139** (SIGSEGV). Fable F2, still open as 🎯T7. Go auto-extension (`go/sqldeep/sqldeep_auto.c:13-14`, `cmd/sqldeep.c:20-22`) installs these functions on every connection in the process.
- **Mechanism:** Runtime functions are ordinary SQLite UDFs. A SQL NULL in a tag/name/key slot (including `SELECT {(NULL): 1}` / nullable computed keys) yields a NULL C string; `strlen`/`memcpy` dereference it and kill the process. Not an error return.
- **Blast radius:** CLI, any C/C++ embedder that registers the runtime, every Go process that imports `github.com/marcelocantos/sqldeep/go/sqldeep`, Kotlin JNI. One nullable column is enough.
- **Counterevidence checked:** Value (not name) NULL is skipped in several loops (e.g. `:140`). Swift `valueText` returns `""` on NULL (`SQLDeepRuntime.swift:93-95`) — so the Swift port does **not** crash, which is binding divergence rather than a C fix. `cv test` / `testdata/sqlite.yaml` have no NULL-tag cases (79 cases, all pass).
- **Smallest coherent remediation:** Guard every tag/name/key read (`SQLITE_NULL` or NULL text → `sqlite3_result_error` or skip). Apply the same contract in Swift so the ports cannot drift. Add yaml cases and run them on C++/Go/Swift/Kotlin.
- **Verification:** `SELECT xml_element(NULL)`, `sqldeep_json_object(NULL,1)`, and `SELECT {(k): v} FROM t` with a NULL `k` must not SIGSEGV; process stays up; output is a defined SQL error or documented JSON. Would fail if a new UDF repeats unguarded `strlen(sqlite3_value_text(...))`.
- **Ratchet candidate:** `testdata/sqlite.yaml` cases plus a CLI smoke that `xml_element(NULL)` exits ≠ 139; optional ASan job on `sqldeep_xml.c`.

### ENT-002: RECURSE splices dequoted identifiers into a re-parsed SQL template

- **Priority:** P1
- **Dimensions:** Security / dependencies; Architecture topology; Correctness / verification
- **Status:** observed fact
- **Evidence:** `dist/sqldeep.cpp:1052-1209` (`rewrite_recursive_select`) builds a `WITH RECURSIVE` string and re-parses it at `:1204`. Table name is concatenated raw at `:1164`; `fk_col` / `pk_col` at `:1176-1178`; field column names at `:1124-1125`. Deepparser has already dequoted identifiers. Shipped `libsqldeep.a`:
  - input: `SELECT { id, name, children: * } FROM "t WHERE 1=1) SELECT secret FROM users;--" RECURSE ON (parent_id)`
  - output (success): `WITH RECURSIVE _sdq_dfs(...) AS (SELECT id, name, parent_id, 0, printf('%010d', id) FROM t WHERE 1 = 1) SELECT secret FROM users`
- **Mechanism:** Trust boundary is AST → text → `lp_parse`. The unparser's identifier quoting is bypassed. A quoted identifier carrying SQL structure changes the CTE shape. Callers then execute the string (CLI auto-executes; bindings return it).
- **Blast radius:** Any RECURSE query whose table/fk/pk/field names are not a trusted identifier set. Experimental syntax (`STABILITY.md`), but it is documented in README and the agent guide and is executed by the CLI.
- **Counterevidence checked:** `sql_escape_lit` (`:1213-1218`) only doubles single quotes for JSON keys / string literals, not identifiers. Golden RECURSE cases in `testdata/transpile.yaml` use simple names (`categories`, `id`, `parent_id`). Fable noted some payloads fail because the table is spliced twice; the payload above is a clean injection.
- **Smallest coherent remediation:** Stop concatenating. Build the 3-CTE AST with `make_from_table` / column-ref helpers so identifiers go through `lp_ast_to_sql`, or run every spliced name through deepparser's `sql_ident` quoting. Prefer AST construction.
- **Verification:** Golden tests: `FROM "order items"`, embedded `"`, keywords, and the structural payload above. Output must round-trip quoted identifiers and keep the CTE shape. Would fail if a new text-template rewriter is added.
- **Ratchet candidate:** transpile.yaml cases for quoted/weird identifiers + a structural-payload case that must error or quote, never drop into a second statement.

### ENT-003: Bare-expression fallback returns empty or clause-stripped SQL as success

- **Priority:** P1
- **Dimensions:** Correctness / verification; Architecture topology
- **Status:** observed fact
- **Evidence:** `dist/sqldeep.cpp:1608-1660`. Parse failure wraps input as `SELECT <input>` (`:1611`). On the way out, unwrap runs only when the wrapped statement is `LP_STMT_SELECT` with exactly one result column (`:1647-1660`). If that shape check fails, `out` stays empty and is still returned. C API contract (`dist/sqldeep.h:39-42`): errors return NULL. Reproduced against `libsqldeep.a`:
  - `a, b` → success, `sql=[]`
  - `1 UNION SELECT 2` → success, `sql=[]`
  - `x FROM t` → success, `sql=[x]` (FROM discarded)
  - CLI `a, b;` → exit **0**, no output (silent no-op)
- **Mechanism:** Historical “give me a fragment” calling convention has no failure branch when the wrapped SELECT is not a single-column expression. Empty string is a successful transpile; the CLI (`vendor/src/shell.c:24639-24645`) executes non-NULL results and returns, so SQLite never sees a syntax error for the empty case.
- **Blast radius:** CLI interactive users (mistypes vanish). Binding callers that treat empty output as valid SQL. Multi-statement `if (sql) out += sql` at `:1662-1665` can also drop a statement if `lp_ast_to_sql` returns NULL.
- **Counterevidence checked:** Successful wrap of true fragments (`<div/>`, `{a, b}` as objects) is the intended product path and is tested. `x FROM t` in the CLI happens to fail later because SQLite rejects `x`; that is not a C API error. 🎯T6 tracks a related CLI issue (transform errors swallowed) but not this fail-open success.
- **Smallest coherent remediation:** Unwrap only when the wrapped SELECT is a single expression with no FROM/WHERE/compound/LIMIT/WITH. Otherwise rethrow the original parse error. Never return success for empty output of non-empty input. Treat `lp_ast_to_sql` NULL as `Error`.
- **Verification:** Golden tests that `a, b`, `1 UNION SELECT 2`, and `x FROM t` return NULL + `err_msg`. CLI test that `a, b;` is non-zero with a syntax error. Would fail if unwrap grows another silent branch.
- **Ratchet candidate:** transpile.yaml `errors` cases + a CLI-level test (also closes part of 🎯T6).

### ENT-004: RECURSE DFS key `printf('%010d', pk)` corrupts JSON for negative and wide integer PKs

- **Priority:** P1
- **Dimensions:** Correctness / verification
- **Status:** observed fact
- **Evidence:** `dist/sqldeep.cpp:1138-1143` and `:1183-1198`. Path is lexicographic `printf('%010d', pk)` (PG: `lpad(..., 10, '0')`); comma placement is numeric `ROW_NUMBER() OVER (PARTITION BY fk ORDER BY pk)`. Shipped CLI, negative ids −2 / −1 / −3:
  ```
  {"id":-2,"name":"root","children":[,{"id":-1,"name":"a","children":[]}{"id":-3,"name":"b","children":[]}]}
  ```
  Query exits 0. Output is not JSON. `testdata/transpile.yaml` locks in the `%010d` template (e.g. expected strings around the RECURSE cases).
- **Mechanism:** Lexicographic order of the padded string disagrees with numeric order for negatives (leading `-`) and for values wider than 10 digits. Rank-1 (no-comma) fragments emit in the wrong place.
- **Blast radius:** RECURSE on signed or 64-bit keys (snowflake ids). STABILITY.md documents “integer PKs only”; it does not document “non-negative, ≤10 digits”, and the feature does not reject other integers — it corrupts silently.
- **Counterevidence checked:** Small positive integer fixtures in sqlite.yaml pass (control). TEXT PKs are out of the documented integer-PK contract but also corrupt silently rather than error.
- **Smallest coherent remediation:** Derive the path segment from `_child_rank` (already numeric, positive, aligned with comma logic), or reject PKs that cannot round-trip the pad. Explicitly error on non-integer PKs.
- **Verification:** sqlite.yaml RECURSE cases with negative and 11-digit ids must `json.loads` and nest correctly; non-integer PK → error. Would fail if the template regresses to `%010d` on the raw pk.
- **Ratchet candidate:** those yaml cases on all drivers.

### ENT-005: `sd_xml_attrs` length pass under-counts structural bytes and `"` expansion

- **Priority:** P1
- **Dimensions:** Security / dependencies; Local code quality
- **Status:** observed fact (arithmetic + CLI abort); inference that the abort is a heap overflow (ASan vs sqlite3 not run)
- **Evidence:** Length reserve at `dist/sqldeep_xml.c:70` is `2 + xml_escaped_len(val)` with comment `/* ="..." */`. Fill writes three structural bytes (`:94` `=`, `:95` `"`, `:105` `"`) plus NUL at `:107`. `xml_escaped_len` (`:22-32`) has no `case '"'`, so each quote counts as 1, while fill emits `&quot;` (`:98`, 6 bytes). Shipped CLI `SELECT CAST(xml_attrs('a', '"""') AS TEXT);` → exit **133**.
- **Mechanism:** Reserved buffer is shorter than the fill pass for every attribute (off-by-one structural) and much shorter when values contain `"`. Heap corruption in a UDF is a process-memory-safety bug on attacker-influenced column text.
- **Blast radius:** Any `xml_attrs` / XML literal with quotes in attribute values (normal HTML). Same process-wide registration as ENT-001.
- **Counterevidence checked:** Swift `xmlEscapeAttr` uses `String` growth (`SQLDeepRuntime.swift:212`) and does not share this allocator bug — another port split. Fable FO1 was finder-only; this audit reproduced an abort on the quoted case. Full ASan attribution was not obtained (did not rebuild sqlite3 with ASan).
- **Smallest coherent remediation:** Count `"` as `&quot;` in the length pass; reserve 3 structural bytes per attribute (space is already counted at `:67`). Assert reserved == written. Prefer one helper used by XML and JSONML/JSX attr builders.
- **Verification:** ASan test that `xml_attrs` on values containing `"<>&` writes exactly `strlen` of the result. CLI must not abort. Would fail if a new attr builder copies the `2 +` formula.
- **Ratchet candidate:** ASan job on `sqldeep_xml.c` plus sqlite.yaml attribute-escaping cases.

### ENT-006: `sqldeep_transpile(NULL)` constructs `std::string` from a null pointer

- **Priority:** P1
- **Dimensions:** Correctness / verification; Security / dependencies
- **Status:** observed fact
- **Evidence:** `dist/sqldeep.cpp:1771-1776` (and `:1789-1790` for `_fk_backend`). No NULL guard. Driver calling `sqldeep_transpile(NULL, …)` against `libsqldeep.a` → exit **139**.
- **Mechanism:** `std::string(nullptr)` is UB (strlen of NULL). FFI callers (cgo, JNI, Swift) can pass NULL on OOM or a missing JNI string.
- **Blast radius:** Every language binding and any C caller that forwards a nullable `char*`.
- **Counterevidence checked:** JNI `GetStringUTFChars` failure returns NULL from the JNI function (`sqldeep_jni.c:36-37`) before calling transpile — good for that path only. Kotlin `transpile` then throws if native returns null (`SQLDeep.kt:34-35`). C API itself is still unguarded.
- **Smallest coherent remediation:** `if (!input) { set_error(…, "input is NULL", 0, 0); return nullptr; }` on every `extern "C"` entry. Catch `std::exception` / `...` in the same functions (FO4, not separately promoted — same seam).
- **Verification:** C-API unit test: NULL input → NULL return + `err_msg`, process alive.
- **Ratchet candidate:** a `tests/test_api.cpp` (or yaml) case compiled into `cv test`.

### ENT-007: Deepparser migration did not move the embed/bindings distribution boundary

- **Priority:** P1
- **Dimensions:** Architecture topology; Build / release / operations; Documentation / governance; Change amplification
- **Status:** observed fact
- **Evidence:**
  - `dist/sqldeep.cpp:16-18` includes `liteparser.h` / `arena.h`.
  - `cvfile:49-52,62-63` correctly compiles five deepparser objects into `libsqldeep.a`.
  - Documented contract still says two files, no link-time deps: `README.md:324-326,354-356`; `STABILITY.md:239-241`; `dist/sqldeep-agents-guide.md:11-13`; `cmd/sqldeep_agent_guide.inc` (embedded copy of the same claim).
  - Kotlin CMake (`kotlin/src/main/jni/CMakeLists.txt:14-24`) compiles `sqldeep.cpp` with `-I dist -I vendor/include` only — no deepparser sources or includes. Last CMake commit `780dd42` (2026-04-07) predates the parser replacement `be78fca` / v0.22.0 (2026-05-20). Same include set fails: `fatal error: 'liteparser.h' file not found`.
  - Go cgo (`go/sqldeep/cgo.go:6-7`) links `${SRCDIR}/../../build/libsqldeep.a` and `build/sqldeep_xml.o`. Those paths are gitignored (`.gitignore:1,6-8`). Module tags exist (`go/sqldeep/v0.23.0`). `go get` from a clean checkout cannot link.
  - Swift `Package.swift:27-33` `unsafeFlags` the same `build/libsqldeep.a`. Not a distributable SPM package.
  - Kotlin Gradle wrapper files are gitignored (`.gitignore:20-22`).
- **Mechanism:** v0.22.0 changed the library from “two translation units” to “sqldeep + deepparser objects”. Docs, Kotlin’s from-source build, and Go/Swift’s prebuilt-archive build were not given a new single owner. Each binding invented a different, now-stale, way to obtain the C++.
- **Blast radius:** Anyone following README to embed; Android/Kotlin consumers; Go module consumers; Swift package consumers. In-repo developers with a populated `build/` do not see the break (`GOWORK=off go test .` passed here in 0.378s).
- **Counterevidence checked:** CLAUDE.md architecture section is correct about deepparser. `NOTICES` attributes the submodule. Demo/tests/CLI via `cv` work. The contradiction is the **embed/bindings** contract, not the cv pipeline.
- **Smallest coherent remediation:** Pick one distribution: (A) document “copy `dist/` plus `vendor/deepparser/src` and compile the five C files”, and fix Kotlin CMake accordingly; or (B) ship `libsqldeep.a` as a release artifact and make cgo/SPM/CMake consume it from a known path. Delete the two-file sentence from README, STABILITY, and the agent guide in the same change. Have Go cgo compile the C++ (or a released static lib), not a gitignored `build/`.
- **Verification:** Fresh checkout without `build/`: Kotlin CMake configures and links; `go test` in the module succeeds; a README embed snippet compiles. Would fail if CMake/cgo omit deepparser again.
- **Ratchet candidate:** CI jobs `go test` (with `cv lib` first or with sources in cgo) and a CMake configure for `kotlin/src/main/jni`; a test that `README.md` does not contain “No external dependencies” unless a script compiling only `sqldeep.cpp` still works.

### ENT-008: CI and release exercise only `cv test`, not the advertised six drivers

- **Priority:** P1
- **Dimensions:** Correctness / verification; Build / release / operations
- **Status:** observed fact
- **Evidence:** `.github/workflows/ci.yml:29-30` is `cv test` on ubuntu-latest and macos-latest. `release.yml` runs `cv test` then `cv shell` and uploads the CLI tarball. README (`:442-455`) and STABILITY shared-test section claim C++, Go, Swift macOS, Swift iOS Simulator, Kotlin JVM, Kotlin Android all run the 79 yaml cases. None of those extra drivers are CI jobs. No sanitizer job. No dependabot. `compile_flags.txt` still lacks `-Ivendor/deepparser/src`.
- **Mechanism:** The yaml corpus is a real shared oracle for C++ (and locally for Go). Absence from CI means Swift/Kotlin/Go packaging (ENT-007) and runtime-port drift (ENT-009) cannot fail the merge gate. ENT-001–005 are all outside `cv test`.
- **Blast radius:** Any change that breaks cgo flags, Swift runtime, or Kotlin JNI is mergeable while master stays green.
- **Counterevidence checked:** `cv test` itself is a solid, hermetic, two-OS gate for the C++ transpile + sqlite integration path (696 assertions). Release does rebuild the CLI. Recursive submodule checkout is set.
- **Smallest coherent remediation:** Add `cv lib && (cd go/sqldeep && GOWORK=off go test .)` to CI (cheap; 0.4s here). Add a `diff` check that yaml copies match `testdata/sqlite.yaml`. Swift/Kotlin as follow-on if the distribution seam is fixed. ASan on `sqldeep_xml.c` for ENT-001/005.
- **Verification:** A CI log line for `go test` and the yaml-copy diff. Would fail if those steps are deleted.
- **Ratchet candidate:** those CI steps; later a hygiene `ci_job` once `hygiene.yaml` exists.

### ENT-009: Swift reimplements the SQLite runtime; yaml and headers are copied without a check

- **Priority:** P2
- **Dimensions:** Redundancy / sources of truth; Change amplification
- **Status:** observed fact (copies and two implementations); inference that they will drift (currently identical yaml/header)
- **Evidence:** Live C runtime is `dist/sqldeep_xml.c` (795 lines). Swift port is `swift/Sources/SQLDeepRuntime/SQLDeepRuntime.swift` (485 lines), including a NULL-safe `valueText` that already disagrees with C on ENT-001. `testdata/sqlite.yaml` is duplicated at `swift/Tests/SQLDeepRuntimeTests/sqlite.yaml` and `kotlin/src/androidTest/assets/sqlite.yaml` (MD5 match today). `dist/sqldeep.h` is duplicated at `swift/Sources/CSQLDeep/include/sqldeep.h` (identical today). `cmd/sqldeep_agent_guide.inc` is a C string copy of `dist/sqldeep-agents-guide.md`. Go and Kotlin desktop correctly read `testdata/` from the repo root (`go/sqldeep/smoke_test.go:37`, `kotlin/desktop_test.kt:27`).
- **Mechanism:** A BLOB-protocol or escaping fix in C does not change Swift. A new sqlite.yaml case does not move unless someone copies two files. README still says Go has a “pure-Go port” (`README.md:380-381`) and Kotlin a “pure Kotlin port” (`README.md:422`; `STABILITY.md:110-111,307`) — they do not, which hides that Swift is the remaining fork.
- **Blast radius:** Swift/iOS users vs everyone else; yaml-driven tests that pass in C++ and fail (or never run) on iOS.
- **Counterevidence checked:** Copies are identical on this snapshot. Sharing testdata across C++ and Go is healthy. A Swift port may be justified if linking `sqldeep_xml.c` on iOS is unacceptable — that is owner intent (residue).
- **Smallest coherent remediation:** Either link `sqldeep_xml.c` from Swift (delete the port) or generate the Swift file from a spec and test both. Replace yaml copies with a build-step copy or SPM resource pointing at `testdata/`. Generate `sqldeep_agent_guide.inc`. One `diff` CI step.
- **Verification:** CI `diff` of the three yaml files and two headers; or a generate script that fails if out of date.
- **Ratchet candidate:** `command: diff -q testdata/sqlite.yaml swift/Tests/SQLDeepRuntimeTests/sqlite.yaml` (and the Kotlin path).

### ENT-010: CLI discards transpile errors and falls through to raw SQL

- **Priority:** P2
- **Dimensions:** Correctness / verification; Build / release / operations
- **Status:** observed fact (code); needs verification for the exact UX of transform-stage vs parse-stage (🎯T6)
- **Evidence:** `vendor/src/shell.c:24632-24649`. On `sqldeep_transpile` failure, `sdErr` is freed and the original text is prepared as SQLite. 🎯T6 (`bullseye.yaml:86-93`) is `identified`. Combined with ENT-003, some failures are success (empty SQL); others become a SQLite syntax error that hides the transformer message (positional `?` reorder, unknown join alias).
- **Mechanism:** The hook cannot tell parse-failure (should fall through for dot-commands / raw SQL) from transform-rejection (should print `sdErr`). The C API does not expose the stage.
- **Blast radius:** Interactive CLI only; library callers already see `err_msg`.
- **Counterevidence checked:** Fall-through is required for `.help`, `EXPLAIN`, and non-sqldeep SQL. ENT-003’s empty success never reaches this branch.
- **Smallest coherent remediation:** Distinguish stages in the C API (or heuristic: if transpile failed and `sqlite3_prepare_v2` also fails, surface `sdErr`). Add the CLI test named in 🎯T6.
- **Verification:** `FROM t WHERE a = ? SELECT { b: ? }` prints the sqldeep message, not `unrecognized token`.
- **Ratchet candidate:** CLI test in `cv test` or a small `tests/test_cli.cpp`.

### ENT-011: Version and architecture documents disagree with `sqldeep.h` and the parser

- **Priority:** P2
- **Dimensions:** Documentation / governance; Redundancy / sources of truth
- **Status:** observed fact
- **Evidence:** `dist/sqldeep.h:9` `SQLDEEP_VERSION "0.23.0"`. `STABILITY.md:27` snapshot v0.23.0 but the version-macros table (`:62-65`) still lists `"0.21.0"` / MINOR 21. `bullseye.yaml` 🎯T1 context (`:28`) still says “Currently at v0.21.0”. 🎯T5 acceptance (`:78`) still requires a hand-written PostgreSQL parser; `STABILITY.md:322-329` and `transpile_impl` say all backends use deepparser. `CLAUDE.md:10-11` still shows `mk example`. `docs/TODO.md` is a completed ON/USING checklist. `docs/convergence-report.md` is frozen at v0.21.0. 🎯T7 points at untracked `docs/audit/fable-2026-07.md`.
- **Mechanism:** Several documents are hand-maintained catalogues of the same facts (version, parser ownership, build command). They drift independently of the header and of `cvfile`.
- **Blast radius:** Agents and humans following STABILITY/CLAUDE/TODO; v1.0 settling (🎯T1) uses STABILITY as acceptance.
- **Counterevidence checked:** `dist/sqldeep.h` and git tags agree on 0.23.0. CLAUDE.md pipeline description is otherwise the best architecture doc in the repo.
- **Smallest coherent remediation:** Make `sqldeep.h` the only version source; generate or test-assert STABILITY’s version row. Strike T5’s PostgreSQL-parser sentence. Delete `docs/TODO.md`. Commit the Fable report under `docs/audits/` (or `docs/audit/`) so 🎯T7 has a tracked artefact. Replace `mk` in CLAUDE.md with `cv`.
- **Verification:** A test or hygiene `file:` check that STABILITY does not contain `"0.21.0"` while the header is 0.23.0.
- **Ratchet candidate:** grep gate, or generate the STABILITY version table.

### ENT-012: Remaining C runtime / Transformer robustness (FO2, FO4, FO5, FO6)

- **Priority:** P2
- **Dimensions:** Local code quality; Security / dependencies
- **Status:** needs verification (static pattern observed; not dynamically reproduced this session except where noted)
- **Evidence:**
  - FO2: `xml_agg_append` `dist/sqldeep_xml.c:201-209` assigns `sqlite3_realloc` then `memcpy` with no NULL check; duplicated at `:446`, `:704`.
  - FO4: C API `catch (const sqldeep::Error&)` only (`:1777`, `:1791`); interior `catch (...)` at `:1641` destroys the arena and rethrows.
  - FO5: `Transformer::transform` (`:329-330`) calls `build_alias_map` without clearing `alias_map_`; one `Transformer` is reused for every statement (`:1636-1638`).
  - FO6: `xml_escaped_len` accumulates in `int` (`:22-32`).
- **Mechanism:** Same classes as ENT-001/005/006 — C UDFs and the FFI boundary — but without a reproduction in this audit. FO5 would silently mis-resolve joins across statements if confirmed.
- **Blast radius:** Large aggregates / OOM (FO2/FO6); host termination on `std::bad_alloc` (FO4); wrong JOIN columns (FO5).
- **Counterevidence checked:** Fable marked these finder-only. ENT-006 already covers NULL input. Not treated as confirmed crashes.
- **Smallest coherent remediation:** After ENT-001/005, add realloc-NULL handling, `catch (...)` at the C boundary, `alias_map_.clear()` at the start of `transform()` (and per-SELECT scoping if sibling aliases can collide), and `size_t` lengths with a toobig error.
- **Verification:** Two-lens or targeted tests listed in Fable FO2/FO4/FO5/FO6.
- **Ratchet candidate:** ASan + the alias golden tests, once confirmed.

### ENT-013: PostgreSQL backend has no execution oracle

- **Priority:** P2
- **Dimensions:** Correctness / verification
- **Status:** observed fact (no live PG tests); inference that emitter drift can ship
- **Evidence:** `tests/test_transpile.cpp:158-166` compares `expected_postgres` strings (110/183 convention cases, 5/5 fk). No job runs SQL against PostgreSQL. STABILITY and README present PG as a supported backend. 🎯T5 context still calls the PG path “unused”.
- **Mechanism:** Function-name substitution (`jsonb_build_object`, `jsonb_agg`, `string_agg`, `lpad`) is tested as text, not as a query that returns JSON.
- **Blast radius:** Any sqlpipe/PG caller. ENT-004’s PG `lpad` path is unexecuted.
- **Counterevidence checked:** Shared Transformer for all backends is a healthy single source; SQLite execution tests are strong.
- **Smallest coherent remediation:** A thin docker/CI job that runs a subset of sqlite.yaml-equivalent cases on Postgres, or an honest STABILITY mark that PG is transpile-only.
- **Verification:** One nested-object query executed on Postgres matching SQLite JSON (modulo jsonb whitespace).
- **Ratchet candidate:** optional `ci_job` or an explicit `state: skipped` hygiene item with reason.

### ENT-014: macOS CLI links Homebrew readline

- **Priority:** P3
- **Dimensions:** Build / release / operations
- **Status:** observed fact (cvfile); needs verification that GitHub `macos-latest` has `/opt/homebrew/opt/readline`
- **Evidence:** `cvfile:15-16,59-60`. `~/.claude/cpp.md` forbids linking Homebrew C/C++ libraries. `cv test` does not hit this path; `cv shell` / release darwin-arm64 does.
- **Mechanism:** Keg-only readline paths are machine-specific; `brew upgrade` and Intel vs ARM prefixes break the CLI link.
- **Blast radius:** `cv shell` and the Homebrew-tap binary, not `libsqldeep.a`.
- **Counterevidence checked:** Vendored SQLite for the shell is otherwise correct. Linux release installs `libreadline-dev` (`release.yml`).
- **Smallest coherent remediation:** Vendor or statically compile readline, or use macOS libedit with the stock SDK.
- **Verification:** `cv shell` on a Mac without Homebrew readline.
- **Ratchet candidate:** release job already builds the CLI; add a non-Homebrew include path.

### ENT-015: Hygiene undeclared; leftover TODO; no CODEOWNERS / secret-scan / AGENTS.md

- **Priority:** P3
- **Dimensions:** Documentation / governance
- **Status:** observed fact
- **Evidence:** no `hygiene.yaml` (validator FileNotFoundError). no repo `AGENTS.md` (only `CLAUDE.md` / `Claude.md`, same inode on this APFS volume). `docs/TODO.md` is a completed checklist. no CODEOWNERS, SECURITY.md, dependabot.
- **Mechanism:** Steady-state controls are not declared, so they cannot drift-fail. Task tracking for ON/USING leaked into a banned TODO file.
- **Blast radius:** Fleet hygiene aggregation treats this repo as empty; agents following global AGENTS.md will not find a project-level file.
- **Counterevidence checked:** CLAUDE.md is substantial and mostly accurate on architecture. LICENSE + NOTICES + SPDX headers on first-party source are present. bullseye.yaml is in use (🎯T1, T6, T7 live).
- **Smallest coherent remediation:** When asked, onboard `hygiene.yaml` from reality (`cv test`, LICENSE, submodule). Delete `docs/TODO.md`. Optional thin `AGENTS.md` pointing at CLAUDE.md.
- **Verification:** `hygiene_check.py` exit 0 against a declared file.
- **Ratchet candidate:** the hygiene file itself, once the owner wants it.

## Redundancy and competing sources of truth

| Fact | Authorities | Drift observed? |
|---|---|---|
| Library version | `dist/sqldeep.h` (0.23.0), git tags, STABILITY macros table (0.21.0), 🎯T1 context (0.21.0) | yes |
| How to embed C++ | README / STABILITY / agent guide (two files); `cvfile` + includes (sqldeep + deepparser) | yes |
| Who parses SQL | CLAUDE.md / STABILITY v0.22 note (deepparser, all backends); 🎯T5 acceptance (hand-written PG parser) | yes |
| SQLite runtime | `sqldeep_xml.c` (C++/Go/Kotlin); `SQLDeepRuntime.swift` | yes (NULL behaviour) |
| sqlite.yaml | `testdata/` (C++/Go/Kotlin desktop); copies in Swift tests and Kotlin androidTest | identical today, unenforced |
| Public C header | `dist/sqldeep.h`; copy in `swift/Sources/CSQLDeep/include/sqldeep.h` | identical today |
| Agent guide | `dist/sqldeep-agents-guide.md`; `cmd/sqldeep_agent_guide.inc` | same two-file claim |
| Runtime ports in docs | README `:380-381` “pure-Go”; README `:422` / STABILITY `:110-111,307` “pure Kotlin”; code is C via cgo/JNI | yes |
| Build command | CLAUDE.md still mentions `mk example`; `cvfile` is the build | yes |
| Fable findings | untracked `docs/audit/fable-2026-07.md`; 🎯T7; this report | T7 not achieved; defects live |

Deliberate / acceptable duplication: Go and C++ both reading `testdata/*.yaml` (one file, two drivers); SPDX headers; `SQLDEEP_SQLITE` vs `SQLDEEP_SQLITE_VANILLA` emitter tables (real variation).

## Healthy structure worth retaining

- **Single Transformer, post-order, in `dist/sqldeep.cpp`.** Dialect knowledge is not scattered across parse and print. Deepparser submodule edge is explicit (`NOTICES`, `.gitmodules` branch `sqldeep-grammar`).
- **Small C API** (`dist/sqldeep.h`, 68 lines) as the only FFI surface. Bindings wrap it rather than reimplementing transpile (except the Swift *runtime*).
- **YAML-driven goldens** (`testdata/transpile.yaml` 213 cases across four buckets; `testdata/sqlite.yaml` 79 execution cases) consumed by C++ doctest and Go tests. This is the right oracle shape.
- **Positional `?` order guard** (`mark_positional_params` / `restore_positional_params`) — a real invariant with tests, added in v0.23.0.
- **FK index vs convention mode** with no silent fallback when metadata is provided (`resolve_fk_columns`).
- **Vendoring policy** followed for doctest, fkYAML, SQLite, deepparser (licenses next to code; NOTICES). Library transpile path still has no SQLite link dependency.
- **`cv test` is hermetic and green** (696/696) on the C++ shipped path.
- **SPDX + Apache-2.0** on first-party sources; CLI `--version` matches the header.

The audit tried to treat the 1800-line Transformer as a cohesion problem and failed: the file is the product. Splitting by node kind without a second owner would amplify change.

## Hygiene posture

`hygiene.yaml` is **absent**. Posture is **not declared**. The validator was invoked from the repo root:

```
/Users/marcelo/.claude/skills/hygiene/hygiene_check.py
→ FileNotFoundError: .../sqldeep/hygiene.yaml
exit 1
```

No floors, held tiers, or drift vector exist to report. This audit did **not** initialize `hygiene.yaml`.

Overlap with entropy: ENT-008 (CI matrix), ENT-014 (readline), ENT-015 (undeclared posture) would become hygiene items if onboarded. Do not ratify floors from this report’s finding count.

Entropy findings that are good future hygiene evidence, if the owner adopts the skill:

- `command: cv test`
- `ci_job: ci.yml#test` (and a future `go-test` job)
- `file: {path: testdata/sqlite.yaml}` plus a `diff -q` command for copies
- `absent:` sanitizer / dependabot if skipped with a reason
- `file:` SPDX/LICENSE (already true)

## Oracle coverage and residue

| Property | Decided by |
|---|---|
| Transpile goldens (SQLite + some PG strings) | shipped `cv test` ← `testdata/transpile.yaml` |
| SQLite execution of 79 yaml cases (C++) | shipped `cv test` ← `testdata/sqlite.yaml` |
| Same 79 cases in Go | auxiliary `go test` (local, needs `build/`); **not CI** |
| Same 79 cases in Swift / Kotlin | drivers exist; **not run this session, not CI** |
| CLI version string | shipped binary `sqldeep 0.23.0` |
| ENT-001 NULL crash | shipped CLI SIGSEGV 139 (no yaml case) |
| ENT-002 identifier injection | shipped `libsqldeep.a` (no yaml case) |
| ENT-003 empty/stripped success | shipped C API + CLI (no yaml case) |
| ENT-004 negative PK JSON | shipped CLI (no yaml case; goldens lock `%010d`) |
| ENT-005 attr buffer | static arithmetic + CLI abort 133; **not ASan** |
| ENT-006 NULL C API input | shipped lib SIGSEGV 139 |
| ENT-007 Kotlin compile | auxiliary compile with CMake flags |
| PostgreSQL execution | **nothing** |
| FO2/FO4/FO5/FO6 | **nothing** this session (static read only) |
| Secret scan / SCA / SBOM | **nothing** |
| Hygiene floors | **undeclared** |

**Owner residue (intent only):**

1. Is the Swift XML/JSON runtime a required iOS isolation, or should Swift link `sqldeep_xml.c` like Go/Kotlin?
2. Is `go/sqldeep` a public `go get` module (then cgo must build from source or a released archive) or an in-repo binding (then README should stop saying `go get`)?
3. After deepparser, is the supported C++ embed “source files + submodule” or “release `libsqldeep.a`”?
4. Is PostgreSQL a supported runtime backend or transpile-only?
5. Accept RECURSE as non-negative ≤10-digit integer PKs (and reject the rest), or fix the sort key for the full integer domain?

Do not hand back “run ASan” or “add the yaml cases” as owner questions — those are mechanical and listed under findings.

## Remediation sequence

1. **Close the crash/corruption seam on the shipped C runtime and C API** (ENT-001, ENT-005, ENT-006, then ENT-012’s `catch (...)` and realloc NULL). This is independent of architecture and unblocks safe Go auto-extension. Verify with yaml + CLI + a NULL-input API test inside `cv test`.
2. **Stop leaving the AST for RECURSE** (ENT-002, ENT-004): quote-via-unparser or build the CTE as nodes; replace `%010d` of the raw pk. Add the injection and negative-PK goldens. That is 🎯T7’s F1/F4.
3. **Make bare-expression unwrap fail closed** (ENT-003) and teach the CLI to surface transform errors (ENT-010 / 🎯T6). Same unwrap function, one CLI hook.
4. **Pick an embed/bindings distribution and move every consumer onto it** (ENT-007): Kotlin CMake + Go cgo + Swift linker flags + README/STABILITY/agent-guide in one change. Then put `go test` and a yaml-copy `diff` in CI (ENT-008, ENT-009).
5. **Reconcile documents with `sqldeep.h` and deepparser** (ENT-011). Delete `docs/TODO.md`. Track the Fable report. Optionally onboard `hygiene.yaml` from the gates that actually exist (`cv test`, LICENSE, submodule). Do not invent floors above reality.
6. **Re-run this audit** on the same finding IDs and the same `cv test` / CLI reproductions.

Do not start with a Transformer rewrite or a new runtime language port. The core topology is the part that should stay.
