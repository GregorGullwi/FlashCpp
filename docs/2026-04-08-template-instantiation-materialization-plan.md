# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-20 (Phase 5: sema-owned materialization at call-resolution and conversion sites)

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
  - **The shared helper itself is now sema-owned.** `AstToIr::materializeLazyMemberIfNeeded` is a thin forwarder to `SemanticAnalysis::ensureMemberFunctionMaterialized`; registry lookup, `parser_.instantiateLazyMemberFunction(...)`, pending-root normalization, and the "mark instantiated" bookkeeping all live in sema now. `ensureSelectedConstructorMaterialized` also routes through this unified helper.
  - **Three new sema-side materialization triggers are now in place** (the second Phase 5 slice):
    - `tryRecoverCallDeclFromStructMembers` — after resolving a template-instantiation member to a stub, calls `ensureMemberFunctionMaterialized` so codegen sees an already-materialized body.
    - `tryAnnotateConversion` — after confirming that a matching conversion operator exists on a template instantiation type, calls `ensureMemberFunctionMaterialized` for the selected operator before the conversion slot is written.
    - `tryResolveCallableOperatorImpl` — after selecting `operator()` on a template instantiation type, calls `ensureMemberFunctionMaterialized` for the selected overload.
  - The remaining codegen fallbacks in `IrGenerator_Visitors_TypeInit.cpp` (deferred member queue, static initializer) are still in place as belt-and-suspenders guards.
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

- `src/IrGenerator_Visitors_TypeInit.cpp` (deferred member queue fallback and static initializer fallback — kept intentionally)
- `src/IrGenerator_Call_Direct.cpp` — `instantiateAndQueueLazyMember` codegen fallback
- `src/IrGenerator_Call_Indirect.cpp` — `instantiateLazySelectedMember` codegen fallback
- `src/IrGenerator_MemberAccess.cpp` — conversion operator lazy instantiation codegen fallback

The three most important sema-side triggers are now in place (second Phase 5 slice):
- `tryRecoverCallDeclFromStructMembers` materializes stubs found during member-call resolution.
- `tryAnnotateConversion` materializes the matching conversion operator during conversion annotation.
- `tryResolveCallableOperatorImpl` materializes `operator()` during callable-object resolution.

The codegen fallback paths (`materializeLazyMemberIfNeeded` calls in `IrGenerator_Call_Direct`,
`IrGenerator_Call_Indirect`, and `IrGenerator_MemberAccess`) are now defensive guards:
sema should have already materialized the body before codegen reaches them.

## Clear next steps

1. **The three key sema-side materialization triggers are in place (second Phase 5 slice)**
   - `tryRecoverCallDeclFromStructMembers` — materializes stubs at member-call resolution time.
   - `tryAnnotateConversion` — materializes conversion operators at conversion annotation time.
   - `tryResolveCallableOperatorImpl` — materializes `operator()` at callable-object resolution time.
   - Codegen fallbacks in `IrGenerator_Call_Direct`, `IrGenerator_Call_Indirect`, `IrGenerator_MemberAccess` are now defensive; sema should have already done the work.

2. **Promote codegen fallbacks to hard assertions (optional future cleanup)**
   - Once confidence is high that sema always materializes before codegen, the `materializeLazyMemberIfNeeded` calls in the three codegen files above can be changed to consistency checks that assert `get_definition().has_value()`.
   - The `IrGenerator_Visitors_TypeInit.cpp` deferred-queue and static-initializer fallbacks should be kept permanently (they handle edge cases outside the sema-call-resolution path).

3. **Delete the codegen bridge once the sema invariant is proven**
   - `AstToIr::materializeLazyMemberIfNeeded` in `IrGenerator_Helpers.cpp` collapses to a hard failure if a supposedly normalized node is still unresolved.

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
- **Done inside Phase 5:** stmt-decl constructor materialization slice; codegen lazy-member bridges consolidated into a single shared helper (`AstToIr::materializeLazyMemberIfNeeded`); that helper is now a thin forwarder to the sema-owned `SemanticAnalysis::ensureMemberFunctionMaterialized`, which also backs `ensureSelectedConstructorMaterialized`; **second slice landed**: sema now triggers `ensureMemberFunctionMaterialized` at the three key call-resolution and conversion-annotation sites (`tryRecoverCallDeclFromStructMembers`, `tryAnnotateConversion`, `tryResolveCallableOperatorImpl`)
- **Next work:** promote remaining codegen fallbacks in `IrGenerator_Call_Direct`, `IrGenerator_Call_Indirect`, `IrGenerator_MemberAccess` from materializing helpers to consistency assertions; then delete the forwarding bridge
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
- `tests/test_phase5_sema_materialization_ret7.cpp` ← new: exercises all three sema triggers

## Validation baseline refreshed on 2026-04-20 (second Phase 5 slice)

Linux validation was re-run after the second Phase 5 slice:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests (includes new `test_phase5_sema_materialization_ret7.cpp`)
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Related docs

- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`
