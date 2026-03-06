# Copilot Fleet: Short Implementation Plan — Identifier Resolution

Purpose
- Implement the refactor described in [docs/2026-03-06_IdentifierResolutionPlan.md](docs/2026-03-06_IdentifierResolutionPlan.md). This file is a compact rally card that points implementers to the canonical plan; do detailed work there.

Core rules (short)
- TDD-first: add the focused failing test(s) before any implementation.
- One small PR per session/phase; keep PRs reviewable and revertible.
- Preserve C++ lookup semantics (using directives, overload sets, ADL, dependent lookup).
- Do not add "Phase X" comments in production code.
- Maintain `Unresolved` as a safe fallback for dependent cases until full correctness is implemented.

Minimal session map (follow the full plan for details)
- Phase 0 — Guardrails: fix SymbolTable overload-set/using semantics; point-of-declaration for initializers. (See docs for split 0A/0B.)
- Phase 1 — AST + Core Binding: add IdentifierBinding + IdentifierNode fields; add createBoundIdentifier(); bind locals/globals/static/enum.
- Phase 1B — Codegen migration: remove detectGlobalOrStaticVar, update generateIdentifierIr() to switch on binding().
- Phase 2 — Lambda captures: set captured bindings in parser; simplify codegen.
- Phase 3 — Templates: leave dependent names Unresolved; bind during instantiation.
- Phase 4 — Function lookup & ADL: collect ordinary + ADL candidates and combine before resolution.
- Phase 5 — Constexpr fast paths / cleanup: use binding for evaluator speedups and final verification.

Branch / PR naming (short)
- Branch prefix: feat/identifier-resolution/
- Example: feat/identifier-resolution/phase-0a
- PR title: "Identifier resolution — <short purpose>"

Tests (one-line)
- Add the focused regression test(s) first (test filenames are given in the docs). Verify they fail, then implement until they pass, then run the broader regression suite.

Reviewer checklist (short)
- Test-first added and demonstrably failing on baseline.
- No "Phase X" markers in source code.
- createBoundIdentifier() consumes SymbolTable ordinary lookup (no ad-hoc bypass).
- Dependent names remain Unresolved until instantiation.
- Overload sets preserved; `using` semantics intact; ADL behavior covered.
- resolved_name_ only used for global/static/member IR generation (not to fake overload selection).
- detectGlobalOrStaticVar removed by the codegen migration PRs (grep → zero).
- `make` / MSVC build succeeds and tests pass.

Next action (recommended)
- Start Phase 0A: add the two `using`/overload tests referenced in the docs under `tests/` on a branch named `feat/identifier-resolution/phase-0a`, push, and open a PR that contains only the tests (so CI shows they fail). Use the full docs file for the detailed step list and file-by-file tasks.