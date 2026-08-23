# Standing invariants for bullseye_convergence.
# Matches hygiene correctness gate: `cv test` is the shipped check.
.PHONY: bullseye
bullseye:
	@cv test && echo "✓ tests"
