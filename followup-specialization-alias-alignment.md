# Agent Prompt: Audit specialization-aligned alias substitution paths

Investigate the class-template instantiation paths that mix primary filled arguments with specialization-aligned pattern arguments, and make alias substitution consistently use the correct argument view at each step.

## Context
- Repository: FlashCpp
- Relevant review concern: `src/Parser_Templates_Inst_ClassTemplate.cpp` has subtle interactions between:
  - `filled_args_for_pattern_match` (primary/default-filled argument view)
  - specialization-aligned pattern argument vectors
- Part of this was fixed in the current PR, but the broader audit was judged valid and too large because multiple alias, substitution, and default-argument paths still depend on choosing the right argument projection.

## Goals
1. Document which operations must use primary/default-filled args vs specialization-pattern-aligned args.
2. Remove accidental mixing of those two views.
3. Preserve correct alias materialization, SFINAE, and default-argument replay behavior.

## Constraints
- No platform-specific commands or tooling assumptions.
- Keep the solution standards-faithful; do not paper over mismatches with fallback recovery.
- Prefer explicit helpers or named abstractions over ad-hoc positional indexing.

## Suggested investigation areas
- `Parser_Templates_Inst_ClassTemplate.cpp`
- alias target materialization helpers
- specialization pattern matching
- dependent default template argument evaluation
- tests involving `enable_if`, alias templates, and specialization-based substitution

## Deliverables
1. A map of which argument view each helper should consume.
2. Refactors or helper APIs that make the distinction explicit.
3. Regression tests for alias-based specializations and defaulted dependent arguments.
4. Validation on the full existing test suite.
