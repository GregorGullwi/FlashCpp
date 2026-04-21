# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-22 (Phase 5 Slice F finalized; Phase 6 positional-fallback fix for explicit variadic deduction)

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
  - **Slice A (conversion operators) is done.** `SemanticAnalysis::tryAnnotateConversion` now eagerly materializes the matching conversion-operator lazy stub through `ensureMemberFunctionMaterialized`, so by the time codegen runs the struct visitor, the instantiated body already lives on the struct and `visitFunctionDeclarationNode` emits IR for it directly. The codegen fallback in `emitConversionOperatorCall` is preserved as defense in depth for paths that do not go through `tryAnnotateConversion` (e.g., explicit `static_cast`, direct member-access of `operator T()`), but it is now a no-op in the common sema-annotated implicit-conversion flow.
  - **Slices B & C (direct / indirect call targets) are done.** `SemanticAnalysis::tryAnnotateCallArgConversionsImpl` now eagerly materializes any lazy, still-un-defined member target (cross-struct direct call, static call on a template instantiation, or dispatched member/virtual call) via `ensureMemberFunctionMaterialized` immediately after the resolved call target is selected and before it is cached into `resolved_direct_call_table_` or handed to argument-conversion annotation. This removes the dependency on the codegen-side `instantiateAndQueueLazyMember` / `instantiateLazySelectedMember` fallbacks being the *first* materialization site; those codegen bridges are preserved as defense in depth for call-expression paths that do not flow through sema's call-argument annotation (e.g., synthesized wrapper calls, late-bound lambda callees).
  - **Slice D (constexpr static-member fallback) is done.** `ConstExpr::Evaluator::find_current_struct_member_function_candidate` no longer drives lazy-member materialization directly. When the evaluation context has sema attached it forwards to `SemanticAnalysis::ensureMemberFunctionMaterialized`; the parser-path (`instantiateLazyMemberFunction` + `normalizePendingSemanticRoots` + `markInstantiated`) is preserved only as a pre-sema fallback. Because the first evaluator pass now materializes on its own, the codegen-side retry in `IrGenerator_Visitors_TypeInit.cpp` no longer seeds the retry with its own `materializeLazyMemberIfNeeded` call — it now only owns its symbol-table rebind + template-binding rebuild.
  - **Slice E (deferred-queue fallbacks) is done.** The ctor-shaped fallback was previously verified dead and deleted. The function-shaped fallback has now been eliminated too: lazy function bodies are materialized through `materializeLazyMemberIfNeeded` at the three sites that feed body-less stubs into `deferred_member_functions_` — the struct visitor's per-member loop in `IrGenerator_Visitors_Decl.cpp`, the `queueDeferredMemberFunctions` lambda in `IrGenerator_Call_Direct.cpp` that snapshots a resolved owner struct's member list, and the per-call `queueDeferredMemberFunctionFromNode` sites in `IrGenerator_Call_Direct.cpp` / `IrGenerator_Call_Indirect.cpp` / `IrGenerator_MemberAccess.cpp` (already materialized). An audit guard that replaced the fallback with a hard failure confirmed zero regressions across the 2171-test suite, after which the fallback in `generateDeferredMemberFunctions` was removed outright.
  - **Slice F (end-of-sema lazy-member drain) landed.** A new helper `SemanticAnalysis::drainLazyMemberRegistry` runs at the tail of `SemanticAnalysis::run` (after `normalizePendingSemanticRoots`). It walks every reachable `StructDeclarationNode` from `parser_.get_nodes()` (recursing into namespaces and nested classes), and for each member listed in that struct's AST `member_functions()` that still has a lazy-registry entry, it calls `ensureMemberFunctionMaterialized`. This mirrors the struct-visitor's per-member filter exactly, so it never over-materializes SFINAE-only instantiations whose members are not ODR-used. The codegen-side `AstToIr::materializeLazyMemberIfNeeded` is no longer a first materializer on the common path — it is a thin forwarder into sema that no-ops when sema has already materialized. A small residual set of instantiated structs (held only through `StructTypeInfo` / lazy-registry references and not reachable from the top-level AST walk) still triggers first-time materialization via the forwarder; this is intentional — unconditionally draining every instantiated struct over-materializes SFINAE-probed template-argument instantiations whose member bodies are ill-formed by design (see step 3 under "Clear next steps").
- **Phase 6:** In progress.
  - The explicit-deduction mapping bug around `positional_deduced_call_arg_index` is fixed: positional fallback now only runs for trailing parameters after a **function-parameter pack**, and it skips call-argument slots already consumed by direct pre-deduction. This closes the bad `template<typename... Rest, typename U> f(Identity<U>::type)` acceptance path where a pure template-parameter pack (with no function-parameter pack) could incorrectly deduce `U` from the first call argument. Regression covered by `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.
  - Broader explicit-deduction cleanup is still open.

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

### Slice A (conversion operators) is done

Sema's existence-check helper `structHasConversionOperatorTo` (used by `tryAnnotateConversion`) now takes a `SemanticAnalysis*` parameter and, when a matching lazy conversion operator is discovered in the source struct (or an inherited base struct), calls `ensureMemberFunctionMaterialized(struct, member, is_const)` as a side effect.

Concrete effect: every implicit `Struct→primitive` user-defined conversion that sema annotates is now guaranteed to have its operator body materialized **before** codegen begins. The codegen struct visitor sees the body and emits IR through the normal `visitFunctionDeclarationNode` path. Codegen's existing materialize-and-queue fallback in `emitConversionOperatorCall` still covers paths that bypass `tryAnnotateConversion` (explicit conversions, direct operator-name access).

Validation: full regression suite (`bash ./tests/run_all_tests.sh`) passes with the same 2170 pass / 147 expected-fail baseline.

## What is still open

### Remaining Phase 5 work

Phase 5 is now the remaining ownership cleanup: shrink the places where codegen still asks the parser to materialize lazy members on demand.

The main remaining surfaces are:

- `src/IrGenerator_Visitors_TypeInit.cpp` *(Slice D done; Slice E partially done — function-shaped deferred-queue fallback still reachable)*
- `src/IrGenerator_Call_Direct.cpp` *(Slices B/C in place in sema: codegen fallback kept only for paths that don't flow through sema's call-arg annotation)*
- `src/IrGenerator_Call_Indirect.cpp` *(Slices B/C in place in sema: fallback kept only for paths that don't flow through sema's call-arg annotation)*
- `src/IrGenerator_MemberAccess.cpp` *(Slice A in place: fallback kept only for non-sema-annotated conversion paths)*

These files used to contain six ad-hoc `parser_->instantiateLazyMemberFunction(...)` bridges with subtly different normalize/mark/queue logic. They have now been funneled through two shared codegen helpers:

- `AstToIr::materializeLazyMemberIfNeeded(struct, member, optional<is_const>)`
- `AstToIr::queueDeferredMemberFunctionFromNode(struct, node, qualified_name_for_ns)`

`materializeLazyMemberIfNeeded` itself is now just a forwarder to the sema-owned `SemanticAnalysis::ensureMemberFunctionMaterialized`, so the registry/normalize/mark logic lives in exactly one place — inside sema.

Concrete effect: codegen's lazy-member materialization surface is now **one** forwarding location (`IrGenerator_Helpers.cpp`) instead of six, and the actual work happens in sema. Ownership is still mixed at the call sites:

- sema owns the helper, the stmt-decl constructor path, the conversion-operator path (Slice A), and the direct / indirect / member call-target path (Slices B and C)
- codegen still owns the constexpr static-member retry fallback (Slice D) and some other "make the body exist now" fallbacks — but only through the single shared forwarder

The mixed model at the remaining call sites is still the real Phase 5 work, but each call site can now be moved individually through a single-point change instead of six.

### What Phase 5 should mean now

Phase 5 should no longer be described as a broad or abstract boundary discussion.
It is now a concrete cleanup with one target invariant:

- **Codegen should consume already-materialized declarations instead of deciding when lazy template members get instantiated.**

The stmt-decl constructor path, the implicit-conversion-operator path, and the sema-annotated direct/indirect call-target paths already satisfy that invariant.
The remaining files above do not.

## Clear next steps

1. **Audit the remaining codegen bridge (now a thin sema forwarder)**
   - `AstToIr::materializeLazyMemberIfNeeded` in `IrGenerator_Helpers.cpp` forwards to `SemanticAnalysis::ensureMemberFunctionMaterialized`; this is the one remaining surface.
   - Callers: `IrGenerator_Visitors_TypeInit.cpp`, `IrGenerator_Call_Direct.cpp`, `IrGenerator_Call_Indirect.cpp`, `IrGenerator_MemberAccess.cpp` (conv-op fallback only).

2. **For each remaining caller, move materialization earlier**
   - make parser/sema publish the selected callable/member before IR lowering needs it
   - keep codegen as a consumer, not a fallback materializer
   - suggested next slices:
     - **Slice B — direct call** (`IrGenerator_Call_Direct.cpp:954`): **done** — `SemanticAnalysis::tryAnnotateCallArgConversionsImpl` materializes the cross-struct lazily-selected direct-call target before caching it in `resolved_direct_call_table_`.
     - **Slice C — indirect / member-access call** (`IrGenerator_Call_Indirect.cpp:1089`): **done** — the same sema site covers member-call targets, so `materialized_member_func_decl` is no longer the first materialization trigger at codegen time.
     - **Slice D — constexpr static-member fallback** (`IrGenerator_Visitors_TypeInit.cpp:512`): **done** — the evaluator routes its lazy-member materialization through `SemanticAnalysis::ensureMemberFunctionMaterialized` when sema is attached, and the codegen-side retry no longer primes with `materializeLazyMemberIfNeeded`.
     - **Slice E — deferred-queue fallbacks** (`IrGenerator_Visitors_TypeInit.cpp:275/289`): **done** — both the ctor-shaped (line 289) and function-shaped (line 275) fallbacks are gone. Lazy function/ctor bodies are now materialized by the sites that feed `deferred_member_functions_` (`IrGenerator_Visitors_Decl.cpp` struct-visitor loop, `IrGenerator_Call_Direct.cpp` `queueDeferredMemberFunctions` snapshot, and the per-call sites that already used `materializeLazyMemberIfNeeded`). The deferred-queue entry-point in `generateDeferredMemberFunctions` is now purely a consumer.

3. **Delete the codegen bridge once the sema invariant is in place**
   - the shared helper has been **collapsed to a consistency check / forwarder** form: `AstToIr::materializeLazyMemberIfNeeded` simply forwards to `SemanticAnalysis::ensureMemberFunctionMaterialized`, which returns `std::nullopt` whenever sema has already materialized the target. On the common path (any struct reachable from `parser_.get_nodes()` through namespaces / nested classes) Slice F's end-of-sema drain has already done the work, so the forwarder is a no-op consistency check.
   - **Outright deletion is intentionally parked.** A residual set of instantiated structs lives only through `StructTypeInfo` / lazy-registry references and is not reachable from the top-level AST walk. The obvious generalization — have `try_instantiate_class_template` track every freshly-instantiated struct in a side list and have `drainLazyMemberRegistry` walk that list too — was implemented and tested; it regressed `tests/test_namespaced_pair_swap_sfinae_ret0.cpp`. The reason is architectural: a class-template instantiation that appears only as a **template argument** feeding a SFINAE probe (e.g., `pair<const int, int>` passed to `is_swappable<...>`) is *not* in SFINAE context at the point where it is instantiated, so an `!in_sfinae_context_` gate does not help. Its member bodies (`pair<const int, int>::swap` in that test) may be ill-formed by design and are only valid to instantiate lazily through actual ODR-use — which never happens in the SFINAE-probe case. Draining all such structs over-materializes those bodies and breaks linking with references to deleted / unresolved overloads.
   - The practical resolution: keep the forwarder as a safe fallback for the 4 residual tests where a codegen call site really does need to drive first-materialization for an unreachable struct. The forwarder contains no materialization logic of its own; it is a thin call into `SemanticAnalysis::ensureMemberFunctionMaterialized`. This preserves the "sema owns materialization" invariant that Slices A–E established.

4. **Re-run the existing template-heavy regression cluster after each slice**
   - pending sema normalization
   - nested template constructor materialization
   - conversion-operator lazy materialization
   - phase-5 constexpr/member-binding regressions

5. **Only after the ownership cleanup is stable, continue Phase 6 cleanup**
   - the `positional_deduced_call_arg_index` bug in `src/Parser_Templates_Inst_Deduction.cpp` is fixed (2026-04-22): positional fallback is now gated on `has_variadic_func_pack` (not `has_variadic_pack`), and it skips call-argument slots recorded in `deduction_info->pre_deduced_arg_indices`. Regression coverage: `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.
   - broader explicit-deduction architecture still needs a follow-up audit
   - this remains separate from the Phase 5 ownership cleanup

## Recommended interpretation of the roadmap

If you want the shortest accurate summary:

- **Done:** Phases 1-4
- **In progress:** Phase 5
- **Done inside Phase 5:** stmt-decl constructor materialization slice; codegen lazy-member bridges consolidated into a single shared helper (`AstToIr::materializeLazyMemberIfNeeded`); that helper is now a thin forwarder to the sema-owned `SemanticAnalysis::ensureMemberFunctionMaterialized`, which also backs `ensureSelectedConstructorMaterialized`; **Slice A (conversion operators) landed** — `tryAnnotateConversion` eagerly materializes the selected conversion-operator body before codegen runs; **Slices B & C (direct / indirect / member call targets) landed** — `tryAnnotateCallArgConversionsImpl` eagerly materializes the selected call target for free/static direct calls and for dispatched member/virtual calls before codegen consumes them; **Slice D (constexpr static-member fallback) landed** — `ConstExpr::Evaluator` routes its lazy-member materialization through sema when attached and the codegen retry no longer primes materialization; **Slice E (deferred-queue fallbacks) landed** — both deferred-queue fallbacks are deleted; the queue-seeding sites in the struct visitor and the cross-struct `queueDeferredMemberFunctions` snapshot now materialize lazy stubs through the sema-owned bridge before pushing, so `generateDeferredMemberFunctions` is purely a consumer; **Slice F (end-of-sema drain) landed** — `SemanticAnalysis::drainLazyMemberRegistry` walks every AST-reachable struct's `member_functions()` and materializes remaining lazy stubs there, preempting the codegen struct-visitor for the overwhelming majority of instantiations; **step 3 (bridge collapse) landed as the consistency-check / forwarder form** — the codegen-side bridge no longer contains any materialization logic, it simply forwards to sema and returns a no-op on the common pre-drained path. The forwarder is **intentionally retained** as a safe fallback for the ~4 tests whose instantiated structs live outside the top-level AST walk; unconditionally draining every instantiated struct over-materializes SFINAE-probed template-argument instantiations (see step 3 above for the detailed rationale and the regressing test).
- **Next work:** Phase 6 explicit-deduction architecture follow-up audit. The Phase 5 ownership invariant is in place: sema materializes every body that codegen needs, and the residual forwarder is a thin on-demand bridge into sema rather than a first-materialization site. The first Phase 6 mechanical fix (positional-fallback gating on `has_variadic_func_pack`) has landed.
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

## Validation baseline refreshed on 2026-04-22 (Slice F finalization; Phase 6 positional-fallback fix)

Windows validation was re-run after:
- reverting an attempted `try_instantiate_class_template` side-list tracking + drain extension that regressed `tests/test_namespaced_pair_swap_sfinae_ret0.cpp` (template arguments to SFINAE probes are instantiated in non-SFINAE context, so gating on `in_sfinae_context_` was insufficient — their member bodies are lazily ill-formed and must not be eagerly drained);
- removing the temporary audit instrumentation from `AstToIr::materializeLazyMemberIfNeeded`;
- rewriting the comments in `drainLazyMemberRegistry` and `materializeLazyMemberIfNeeded` to document the intentional scope of the drain and why the forwarder is retained;
- landing the Phase 6 positional-fallback fix in `try_instantiate_template_explicit` (gate on `has_variadic_func_pack`, skip pre-deduced call-arg slots) with new regression test `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected (+1 from new `test_explicit_variadic_pack_nondeduced_tail_fail.cpp`)
- overall result: **SUCCESS**

Slice F is complete in its landing form: the sema-owned drain is the first materializer for every struct reachable from `parser_.get_nodes()`, and the residual codegen forwarder is an on-demand bridge that delegates to sema rather than performing materialization itself.

## Validation baseline refreshed on 2026-04-21 (Slice F partial completion)

Linux validation was re-run after adding `SemanticAnalysis::drainLazyMemberRegistry` and collapsing `AstToIr::materializeLazyMemberIfNeeded` to a pure sema forwarder:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

A temporary audit guard was inserted in the forwarder that threw `InternalError` whenever sema was about to perform first-time materialization at codegen time. With only Slices A-E in place it caught 34 struct-visitor tests (confirming the struct-visitor was the remaining codegen-first site). After Slice F's drain was added, the same guard fell to 4 tests — all of them involving instantiated structs that live outside the top-level AST walk. Those residual tests are the only reason the forwarder still sees first-time work; they are the tracking item for step 3's outright deletion.

## Validation baseline refreshed on 2026-04-20 (Slice E completion)

Linux validation was re-run after removing the function-shaped deferred-queue fallback:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

An audit guard that converted the removed fallback into a hard `InternalError` was run against the full suite before deletion and was never triggered, confirming the fallback is genuinely unreachable with the new sema-first materialization at the queue-seeding sites.

## Related docs

- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`
