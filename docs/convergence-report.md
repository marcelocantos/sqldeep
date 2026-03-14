# Convergence Report

*Evaluated: 2026-03-15*

Standing invariants: all green. Tests: 44 tests, 312 assertions passing.

## Movement

- 🎯T1: close → blocked (v0.7.0 introduced breaking changes and Experimental items; settling clock reset)
- 🎯T2: unchanged (significant gap — parser/renderer still needed)

## Gap Report

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable  [high]
Gap: **blocked**
v0.7.0 changes reset the settling clock: `//` comments removed (breaking), SQL comments (`--`, `/* */`) added as Experimental, `->` FROM-context restriction added as Experimental, `->>` passthrough added as Experimental. These need real-world usage to confirm the design before promotion to Stable. The C API and all pre-v0.7.0 syntax items remain Stable. 1.0 requires all Experimental items to be promoted and the settling threshold met.

### 🎯T2 Recursive tree construction from self-referential tables  [high]
Gap: **significant**
AST structs defined. Parser, renderer, PostgreSQL backend, and tests all still needed. No progress since last evaluation.

## Recommendation

Work on: **🎯T2 Recursive tree construction**
Reason: 🎯T1 is blocked on settling time — no implementation work will advance it. 🎯T2 has the highest weight (5) and is the only target with actionable implementation work remaining. Completing 🎯T2 also contributes to 🎯T1's settling period (it will land as an Experimental feature in a pre-1.0 release, giving it time to stabilise alongside the v0.7.0 changes).

## Suggested action

Release v0.7.0 first (commit pending changes, run `/release`), then begin 🎯T2 implementation (parser → renderer → tests).

<!-- convergence-deps
evaluated: 2026-03-15T00:00:00Z
sha: d4b1665

🎯T1:
  gap: blocked
  assessment: "Settling clock reset by v0.7.0 breaking changes. Three Experimental items need promotion. No implementation work will advance this."
  read:
    - STABILITY.md
    - dist/sqldeep.h

🎯T2:
  gap: significant
  assessment: "AST structs added, SQL template proven. Parser, renderer, PostgreSQL backend, and tests all still needed."
  read:
    - dist/sqldeep.cpp
-->
