# Convergence Report

*Evaluated: 2026-03-14*

Standing invariants: all green. CI passing on master (c1e87d9). Tests: 42 tests, 298 assertions passing.

## Movement

- 🎯T1: close → close (uncommitted changes now merged; only tagged release remains)
- 🎯T2: (unchanged)

## Gap Report

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable  [high]
Gap: **close**
All code-level acceptance criteria are met and merged to master: STABILITY.md has every item marked Stable, version macros read 1.0.0, `sqldeep_version()` returns "1.0.0", issue #10 is closed. The previously-uncommitted changes were committed in `bcf735f` and merged. The sole remaining criterion is cutting the tagged GitHub release `v1.0.0`.

### 🎯T2 Recursive tree construction from self-referential tables  [high]
Gap: **significant**
AST structs (`RecursiveSelect`, `bool recursive` on fields) are defined. The bracket-injection SQL template has been proven working in SQLite. However, the parser (`parse_recursive_select`, `*` field handling), renderer (`render_recursive_select`), PostgreSQL backend adaptation, and all tests (transpilation + integration) remain unimplemented. No progress since last evaluation.

## Recommendation

Work on: **🎯T1 sqldeep reaches v1.0.0**
Reason: Both targets have equal effective weight (4.3), but 🎯T1 has a much smaller gap — it requires only cutting a release, not development work. Completing 🎯T1 first establishes the stability baseline, after which 🎯T2 can land as a 1.1.0 feature.

## Suggested action

Run `/release` to cut the v1.0.0 release. All code is merged to master and CI is green — no further code changes needed.

<!-- convergence-deps
evaluated: 2026-03-14T00:00:00Z
sha: c1e87d9

🎯T1:
  gap: close
  assessment: "All code criteria met and merged. Only the tagged v1.0.0 GitHub release remains."
  read:
    - STABILITY.md
    - dist/sqldeep.h
    - dist/sqldeep.cpp

🎯T2:
  gap: significant
  assessment: "AST structs added, SQL template proven. Parser, renderer, PostgreSQL backend, and tests all still needed."
  read:
    - dist/sqldeep.cpp
-->
