# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-20

This document is now a short status audit, not a historical scratchpad.
Its purpose is to answer two questions clearly:

1. What is already done?
2. What are the next concrete steps?

## Current status

- **Phase 0:** Done. The original regression cluster remains the guardrail.
- **Phase 1:** Done. Non-type template-argument identity now has one canonical key path.
- **Phase 2:** Done. Alias-template materialization is centralized through shared helpers.
- **Phase 3:** Done. Late-materialized roots now have an explicit register/normalize lifecycle.
- **Phase 4:** Done. Dependent placeholder state is explicit via `DependentPlaceholderKind`.
- **Phase 5:** **In progress.**
  - The first constructor-materialization slice is done (sema materializes stmt-decl ctors).
  - The remaining codegen bridges are now funneled through a **single shared helper** (`AstToIr::materializeLazyMemberIfNeeded`), shrinking the surface for the next sema-ownership move.
  - The broader parser/sema ownership move is still open.
- **Phase 6:** Not done. A separate explicit-deduction mapping issue is still open.

## What is clearly landed

### Phases 1-4 are complete

These are no longer the active work items:

- **Phase 1:** `NonTypeValueIdentity` is the canonical NTTP identity path.
- **Phase 2:** alias-template materialization now routes through shared helpers instead of duplicated ad hoc paths.
- **Phase 3:** `Parser.h` documents the late-materialization lifecycle and exposes the canonical registration helpers:
  - `registerLateMaterializedTopLevelNode(...)`
  - `registerAndNormalizeLateMaterializedTopLevelNode(...)`
  - `normalizePendingSemanticRootsIfAvailable()`
- **Phase 4:** `DependentPlaceholderKind` exists on `TypeInfo`, and the important placeholder consumers now use typed state instead of string heuristics.

### The first real Phase 5 slice is also done

The variable-declaration constructor path is no longer doing its own template-constructor materialization in stmt-decl codegen.

What changed:

- `src/IrGenerator_Stmt_Decl.cpp` now expects sema/materialized constructor information instead of instantiating template constructors itself.
- `src/SemanticAnalysis.cpp` now materializes the selected constructor earlier for the direct-init and brace-init variable-declaration paths.
- Nested/out-of-line constructor materialization for the exercised paths is covered by regression tests.

So the repo is **past “Phase 5 can begin”**. It has already begun.

## What is still open

### Remaining Phase 5 work

Phase 5 is now the remaining ownership cleanup: shrink the places where codegen still asks the parser to materialize lazy members on demand.

The main remaining surfaces are:

- `src/IrGenerator_Visitors_TypeInit.cpp`
- `src/IrGenerator_Call_Direct.cpp`
- `src/IrGenerator_Call_Indirect.cpp`
- `src/IrGenerator_MemberAccess.cpp`

These files used to contain six ad-hoc `parser_->instantiateLazyMemberFunction(...)` bridges with subtly different normalize/mark/queue logic. They have now been funneled through two shared codegen helpers:

- `AstToIr::materializeLazyMemberIfNeeded(struct, member, optional<is_const>)`
- `AstToIr::queueDeferredMemberFunctionFromNode(struct, node, qualified_name_for_ns)`

Concrete effect: codegen's lazy-member materialization surface is now **one** location (`IrGenerator_Helpers.cpp`) instead of six. Ownership is still mixed:

- sema owns some materialization already
- codegen still owns some "make the body exist now" fallbacks — but only through the single shared helper

The mixed model is still the real remaining Phase 5 work, but the cleanup is now a single-point change instead of six.

### What Phase 5 should mean now

Phase 5 should no longer be described as a broad or abstract boundary discussion.
It is now a concrete cleanup with one target invariant:

- **Codegen should consume already-materialized declarations instead of deciding when lazy template members get instantiated.**

The stmt-decl constructor path already satisfies that invariant.
The remaining files above do not.

## Clear next steps

1. **Audit the remaining codegen bridge (now a single helper)**
   - `AstToIr::materializeLazyMemberIfNeeded` in `IrGenerator_Helpers.cpp` is the one remaining surface.
   - Callers: `IrGenerator_Visitors_TypeInit.cpp`, `IrGenerator_Call_Direct.cpp`, `IrGenerator_Call_Indirect.cpp`, `IrGenerator_MemberAccess.cpp`.

2. **For each caller, move materialization earlier**
   - make parser/sema publish the selected callable/member before IR lowering needs it
   - keep codegen as a consumer, not a fallback materializer

3. **Delete the codegen bridge once the sema invariant is in place**
   - the shared helper then collapses to a consistency check / hard failure if a supposedly normalized node is still unresolved

4. **Re-run the existing template-heavy regression cluster after each slice**
   - pending sema normalization
   - nested template constructor materialization
   - conversion-operator lazy materialization
   - phase-5 constexpr/member-binding regressions

5. **Only after the ownership cleanup is stable, pick up Phase 6**
   - `src/Parser_Templates_Inst_Deduction.cpp` still uses `positional_deduced_call_arg_index`
   - that is a separate deduction-architecture bug, not the main remaining Phase 5 blocker

## Recommended interpretation of the roadmap

If you want the shortest accurate summary:

- **Done:** Phases 1-4
- **In progress:** Phase 5
- **Done inside Phase 5:** stmt-decl constructor materialization slice; codegen lazy-member bridges consolidated into a single shared helper (`AstToIr::materializeLazyMemberIfNeeded`)
- **Next work:** move materialization of the remaining codegen-consumed lazy members into sema, then delete the shared codegen bridge
- **Separate later follow-up:** Phase 6 explicit-deduction mapping cleanup

## Regression coverage worth keeping close

The following tests are the most relevant guardrails for this area:

- `tests/test_pending_sema_normalization_ret0.cpp`
- `tests/test_template_nested_ctor_materialized_before_codegen_ret42.cpp`
- `tests/test_sfinae_dependent_member_ret0.cpp`
- `tests/test_dependent_alias_chain_placeholder_ret42.cpp`
- `tests/test_sizeof_dependent_member_type_ret8.cpp`
- `tests/test_placeholder_kind_mixed_types_ret100.cpp`
- `tests/test_conv_op_sema_phase5_ret42.cpp`
- `tests/test_phase5_nested_templates_ret42.cpp`
- `tests/test_phase5_multi_level_ret45.cpp`

## Validation baseline refreshed on 2026-04-20

Linux validation was re-run during this audit:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2170** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Related docs

- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`
