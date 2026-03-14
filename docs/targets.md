# Targets

<!-- last-evaluated: 149089b -->

## Active

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable
- **Weight**: 4 (value 13 / cost 3)
- **Estimated-cost**: 3
- **Acceptance**:
  - All items in STABILITY.md are marked **Stable** (no Experimental or Needs review remaining)
  - STABILITY.md "Gaps and prerequisites" section is resolved or moved to "Out of scope"
  - Settling threshold met (N consecutive minor releases with zero breaking changes)
  - Version macros in `sqldeep.h` read `1.0.0`
  - `sqldeep_version()` returns `"1.0.0"`
  - GitHub issue #10 is closed (resolved by computed key syntax)
  - Tagged release `v1.0.0` exists on GitHub
- **Context**: v0.7.0 introduced Experimental items (SQL comments, `->` FROM-context restriction, `->>` passthrough) and a breaking change (removed `//` comments). v0.8.0 adds recursive select (also Experimental). The settling clock requires these features to prove out through real-world usage before promotion to Stable.
- **Status**: blocked (settling period — need Experimental items to prove out)
- **Discovered**: 2026-03-12

## Achieved

### 🎯T2 Recursive tree construction from self-referential tables
- **Achieved**: 2026-03-15 (commit 149089b)
- **Acceptance**: All criteria met — parser, renderer, both backends, singular/forest modes, explicit PK, integration tests with 3+ levels, transpilation tests. 47 tests, 325 assertions passing.
- **Discovered**: 2026-03-12
