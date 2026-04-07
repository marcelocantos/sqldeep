# Convergence Report

*Evaluated: 2026-04-07*

Standing invariants: all green. Tests: 8 test cases, 474 assertions passing. CI: last run succeeded (master, v0.20.0 release).

## Movement

- 🎯T1: blocked → blocked (unchanged — new Experimental features in v0.17.0–v0.20.0 continue to reset the settling clock)
- 🎯T2: significant → achieved (completed 2026-03-15)
- 🎯T3: (new, already achieved — v0.9.0)
- 🎯T4: (new, already achieved — v0.12.0)

## Gap Report

### 🎯T1 sqldeep reaches v1.0.0 with all syntax and API surfaces declared stable  [weight 4.3]
Gap: **blocked**

The only active target. 1 of 7 acceptance criteria met (issue #10 closed). The settling clock has been reset repeatedly — v0.7.0 introduced SQL comments and `->` FROM-context restriction, v0.8.0 added recursive select, v0.9.0–v0.14.0 added XML/JSONML/JSX, v0.17.0 introduced custom JSON functions and BLOB protocol, v0.20.0 added Swift and Kotlin bindings. STABILITY.md now lists ~45 Experimental items across C API, Go/Swift/Kotlin bindings, input syntax, output semantics, and parser behaviour. The "Gaps and prerequisites" section has 16 items pending settling.

No implementation work will advance this target. Progress requires a period of stability — no new Experimental features — while existing features prove out through real-world usage. Once features have settled, the path is: promote Experimental items to Stable in STABILITY.md, resolve gaps, bump version to 1.0.0, tag release.

## Recommendation

Work on: **🎯T1 sqldeep reaches v1.0.0** (the only active target)

Reason: While 🎯T1 itself is blocked on settling time, there is no other active target to work on. The most productive next step is to begin the settling process by consciously pausing new feature development and focusing on real-world usage. If the user has sqlpipe or other projects consuming sqldeep, exercising the Experimental features there would advance the settling clock. Alternatively, if the user has new feature ideas, those should be captured as new targets rather than added directly — each new Experimental feature delays 1.0.

## Suggested action

Review the 16 "Gaps and prerequisites" items in STABILITY.md. For features that have been exercised in real-world usage (e.g., SQL comments, `->` FROM-context, recursive select), consider promoting them from Experimental to Stable. Each promotion removes a blocker from the 1.0 path. Run `/target` to create a sub-target for a batch of promotions if several features are ready.

<!-- convergence-deps
evaluated: 2026-04-07T00:00:00Z
sha: 2408fd0

🎯T1:
  gap: blocked
  assessment: "45+ Experimental items in STABILITY.md. Settling clock reset by v0.20.0 (Swift/Kotlin bindings). 16 gaps-and-prerequisites items pending. 1/7 acceptance criteria met (issue #10 closed). No implementation work advances this — needs real-world usage and feature freeze."
  read:
    - STABILITY.md
    - dist/sqldeep.h
    - docs/targets.md
-->
