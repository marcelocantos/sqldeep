# Convergence Report

*Evaluated: 2026-04-08*

Standing invariants: all green. Tests: 8 test cases, 474 assertions passing. CI: last run succeeded (master, v0.21.0 release).

## Movement

- 🎯T1: blocked → blocked (unchanged — v0.21.0 added SQLDEEP_SQLITE_VANILLA backend, another Experimental item)

## Gap Report

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable  [weight 4.3]
Gap: **blocked**

The only active target. 1 of 7 acceptance criteria met (issue #10 closed). Since the last evaluation, v0.21.0 added the `SQLDEEP_SQLITE_VANILLA` backend (Experimental), further extending the settling period. STABILITY.md now lists ~70 Experimental items across C API, XML runtime API, Go/Swift/Kotlin bindings, input syntax, output semantics, and parser behaviour. The "Gaps and prerequisites" section has 16 items pending settling.

No implementation work will advance this target. Progress requires a period of stability — no new Experimental features — while existing features prove out through real-world usage. Once features have settled, the path is: promote Experimental items to Stable in STABILITY.md, resolve gaps, bump version to 1.0.0, tag release.

## Recommendation

Work on: **🎯T1 sqldeep reaches v1.0.0** (the only active target)

Reason: Both the markdown evaluation and bullseye agree — 🎯T1 is the sole active target and is unblocked in the dependency graph (the "blocked" status refers to the settling period, not a structural dependency). While no code changes will directly close the gap, the most productive path is to begin promoting Experimental features that have proven stable through real-world usage. Each promotion shrinks the gap toward 1.0.

## Suggested action

Review the 16 "Gaps and prerequisites" items in STABILITY.md. Features that have been stable across multiple releases without design changes are candidates for promotion from Experimental to Stable. Start with the oldest items: comment syntax (`--`/`/* */`, introduced v0.7.0), `->` FROM-context restriction (v0.7.0), and `->>` passthrough (v0.7.0) — these have survived 14 minor releases without changes. Run `/target` to create a sub-target for a batch of promotions if several features are ready.

## Bullseye scorecard

**Ranking**:        0
**Blocking**:       0
**Data quality**:   -1
**Overall**:        0
**Markdown rec**:   🎯T1 sqldeep reaches v1.0.0
**Bullseye rec**:   🎯T1 sqldeep reaches v1.0.0
**Notes**: Ranking 0: trivial case with only one active target — both systems agree. Blocking 0: no dependency edges to test, both correctly identify T1 as workable. Data quality -1: freshly bootstrapped from markdown, so bullseye has no information that markdown doesn't; the "Identified" default status didn't reflect the real "blocked/converging" state and needed manual correction. Overall 0: equivalent this run — with a single active target, there's nothing for bullseye to differentiate on. A more meaningful comparison requires multiple active targets with dependencies.

<!-- convergence-deps
evaluated: 2026-04-08T00:00:00Z
sha: 39c4a63

🎯T1:
  gap: blocked
  assessment: "~70 Experimental items in STABILITY.md. Settling clock extended by v0.21.0 (SQLDEEP_SQLITE_VANILLA). 16 gaps-and-prerequisites items pending. 1/7 acceptance criteria met (issue #10 closed). No implementation work advances this — needs real-world usage and feature freeze."
  read:
    - STABILITY.md
    - dist/sqldeep.h
    - docs/targets.md

bullseye:
  ranking: 0
  blocking: 0
  data_quality: -1
  overall: 0
  markdown_rec: T1
  bullseye_rec: T1
-->
