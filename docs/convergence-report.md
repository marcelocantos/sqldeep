# Convergence Report

*Evaluated: 2026-03-12*

Standing invariants: all green. CI last passed on master (e405780). Tests: 42 tests, 298 assertions passing.

## Gap Report

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable  [high]
Gap: **close**
All code-level acceptance criteria are met: STABILITY.md has every item marked Stable with design decisions documented, version macros and `sqldeep_version()` read 1.0.0, issue #10 is closed, and the gaps section has been replaced with "Design decisions" and "Out of scope." The only remaining criterion is cutting the tagged GitHub release `v1.0.0`. Uncommitted changes in `STABILITY.md`, `sqldeep.h`, `sqldeep.cpp`, and `sqldeep-agents-guide.md` need to be committed and merged first.

### 🎯T2 Recursive tree construction from self-referential tables  [high]
Gap: **significant**
AST structs have been added (`RecursiveSelect`, `bool recursive` on fields, variant entry) and the bracket-injection SQL template is proven working in SQLite. However, the parser (`parse_field` for `*`, `parse_recursive_select`), renderer (`render_recursive_select`), PostgreSQL backend adaptation, and all tests (transpilation + integration) are still unimplemented. The implementation plan is detailed and clear, but represents substantial coding work.

## Recommendation

Work on: **🎯T1 sqldeep reaches v1.0.0**
Reason: 🎯T1 is the closest to completion (gap: close vs significant) and both targets have equal effective weight (4.3). Releasing 1.0.0 establishes the stability baseline, after which 🎯T2 (recursive trees) can land as a 1.1.0 feature. The uncommitted changes are already done — this is a commit-and-release task, not a development task.

## Suggested action

Commit the staged changes (STABILITY.md, sqldeep.h, sqldeep.cpp, sqldeep-agents-guide.md), then run `/push` to create a PR and get CI green. Once merged, run `/release` to cut v1.0.0.

<!-- convergence-deps
evaluated: 2026-03-12T00:00:00Z
sha: e405780

🎯T1:
  gap: close
  assessment: "All code criteria met. Only the tagged v1.0.0 release remains. Uncommitted changes need commit+merge first."
  read:
    - STABILITY.md
    - dist/sqldeep.h
    - dist/sqldeep.cpp
    - dist/sqldeep-agents-guide.md

🎯T2:
  gap: significant
  assessment: "AST structs added, SQL template proven. Parser, renderer, PostgreSQL backend, and tests all still needed."
  read:
    - dist/sqldeep.cpp
    - dist/sqldeep.h
-->
