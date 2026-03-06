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
- Phase 3 — Templates: replace parsing_template_body_ bool with depth counter; add TemplateParameter binding; guard dependent-base lookup; re-bind after instantiation via AST walk. (See docs for full gap list.)
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
- Dependent names remain Unresolved/TemplateParameter until instantiation.
- Overload sets preserved; `using` semantics intact; ADL behavior covered.
- resolved_name_ only used for global/static/member IR generation (not to fake overload selection).
- detectGlobalOrStaticVar removed by the codegen migration PRs (grep → zero). ✅ Done.
- parsing_template_body_ assignments removed (replaced with depth counter) after Phase 3.
- `make` / MSVC build succeeds and tests pass.

Progress (branch: `copilot/identifier-refactor`, rebased on origin/main `3e4c591c`)
- Phase 0A — **DONE** (no code change needed; using/overload-set semantics already correct in SymbolTable)
- Phase 0B — **DONE** (`c264c2f3`): Fixed point-of-declaration for local variables. Test: `test_initializer_point_of_declaration_ret4.cpp`.
- Phase 1A — **DONE** (`b8ca5961`): `IdentifierBinding` enum (12 variants), extended `IdentifierNode`, `createBoundIdentifier()` in `Parser.h`, 16 creation sites updated, `StaticLocal`/`StaticMember` detected, typo `idenfifier_token` fixed.
- Phase 1B — **DONE** (`ecb1ddf6`, `77ab43db`): `GlobalStaticVarInfo` and `detectGlobalOrStaticVar()` fully deleted; all codegen paths switch on `binding()`; grep = 0. All 1327 tests pass.
- Phase 2 — **DONE** (`1abfe6cd` docs, `5c7437db` impl): `lambda_capture_stack_` added to `Parser`; `createBoundIdentifier()` sets `CapturedByValue`/`CapturedByRef` for explicit named captures; codegen uses `binding()` first with runtime fallback for `[=]`/`[&]` capture-all. Test: `test_lambda_this_implicit_member_ret0.cpp`. All 1328 tests pass.
- Phase 3 — **NEXT**. Key gaps: `parsing_template_body_` bool needs depth counter; `has_deferred_base_classes` not checked in `createBoundIdentifier()`; substitution files don't create new IdentifierNodes — re-bind via post-instantiation AST walk instead. New regression tests listed in docs.
- Phases 4-5 — Pending.

Next action
- Phase 3: Replace `parsing_template_body_` bool with `parsing_template_depth_` counter (RAII guards in `Parser_Templates_Class.cpp` and `Parser_Templates_Function.cpp`); add `TemplateParameter` binding variant to `createBoundIdentifier()`; add dependent-base guard before `NonStaticMember` check. Add regression tests listed in `docs/2026-03-06_IdentifierResolutionPlan.md`.