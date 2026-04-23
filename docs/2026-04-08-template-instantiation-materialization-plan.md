# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-23 (Phase 6 fold-expression complex-pack parser fix)

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
- **Phase 5:** Done.
  - The materialization-ownership cleanup is complete. Sema owns lazy-member materialization, the ODR-use drain is in place, and codegen no longer acts as a first materializer.
  - The long Phase 5 slice chain (A through M) is effectively closed by the current tree state captured below in the validation history.
- **Phase 6:** In progress.
  - The 2026-04-22 positional-fallback bugfix landed: fallback is gated on an actual **function-parameter pack** and skips pre-deduced call-arg slots. Regression: `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.
  - The explicit-deduction cleanup slice landed: explicit function-template instantiation now reuses the canonical function-parameter → call-argument mapping for pack slices, including **mixed explicit + deduced pack** cases. Regressions: `tests/test_explicit_variadic_pack_deduction_ret0.cpp` and `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`.
  - **2026-04-23 multi-pack slice landed:** Two bugs fixed for templates that have multiple variadic packs (e.g. `template<int... Ns, typename... Ts>`):
    1. `try_instantiate_template_explicit`: the call-arg-slice deduction check is now gated on the template-parameter pack that actually maps to the function-parameter pack (via the new `CallArgDeductionInfo::function_pack_template_param_name` field), preventing a false `overload_mismatch` for non-function packs.
    2. `substituteTemplateParameters` / `SizeofPackNode` handler: the accurate per-pack element count is now propagated from `try_instantiate_template_explicit` through the new `template_param_pack_sizes_` member, so `sizeof...(Ns)` and `sizeof...(Ts)` resolve to the correct individual counts rather than the overcounted `template_args.size() - non_variadic_count`. Regressions: `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp`, `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`, `tests/test_multi_pack_sizeof_deduction_ret0.cpp`.
  - **2026-04-23 fold-expression complex-pack parser fix landed:** Two parser patterns extended to handle non-bare-identifier pack expressions per the C++17/20 standard (`[expr.prim.fold]`):
    1. **Pattern 1** `(... op cast-expr)`: the unary-left-fold handler now tries to parse a full cast expression when the token after `... op` is not a bare identifier. Previously any expression like `(args > 0)` was silently rejected and caused a template instantiation failure for common patterns such as `(... && (args > 0))`.
    2. **Pattern 3** `(init op ... op cast-expr)`: the binary-left-fold handler received the same extension for the right-hand operand, enabling `(0 + ... + (args * 2))`.
    A new `FoldExpressionNode` constructor was added for the binary-fold + complex-pack case. Regression: `tests/test_fold_expr_complex_pack_ret0.cpp`.
  - **What is still open:** explicit-deduction handling for pack parameters that appear inside template-specialisation arguments in function-parameter packs during *non-explicit* (deduction-only) calls (e.g. implicit deduction of `Ts` from `Box<Ts>...` arguments). Explicit instantiation of those patterns already works. The broader "other non-trivial pack remapping shapes" class is now well covered for the explicit path; the non-explicit deduction path is the remaining open surface.

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
- `src/IrGenerator_Call_Direct.cpp` *(historical slice; no longer an open Phase 5 surface)*
- `src/IrGenerator_Call_Indirect.cpp` *(historical slice; no longer an open Phase 5 surface)*
- `src/IrGenerator_MemberAccess.cpp` *(historical slice; no longer an open Phase 5 surface)*

These notes are now purely historical. The Phase 5 ownership cleanup is complete; the only remaining active roadmap item is Phase 6 explicit deduction.

### Remaining Phase 6 work

The materialization / ownership plan is effectively closed by the current tree state. The remaining follow-up is the **generalized explicit-deduction architecture**.

Covered by the current tree after this PR:

- trailing non-pack parameters after a function-parameter pack
- pack-before-tail signatures
- mixed explicit + deduced elements within the same function-parameter-pack slice
- the nondeduced-tail bug fixed by `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- template-parameter packs not mapped to any function-parameter pack (type and non-type), covered by `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp` and `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`
- multiple variadic packs where only one is expanded in the function signature, covered by `tests/test_multi_pack_sizeof_deduction_ret0.cpp`
- fold expressions whose pack operand is a complex cast-expression rather than a bare identifier: `(... op (expr))` and `(init op ... op (expr))`, covered by `tests/test_fold_expr_complex_pack_ret0.cpp`

Still open after this PR:

- **non-explicit deduction** of pack elements from `Box<Ts>...`-style function parameters: when `Ts` is expanded inside a template-specialisation argument in the function parameter list, implicit deduction from a call like `sum_boxes(a, b)` incorrectly deduces `Ts = {Box<int>, Box<double>}` instead of `Ts = {int, double}`. Explicit instantiation (`sum_boxes<int, double>(a, b)`) works correctly.
- any other richer template-param ↔ function-param mapping shapes that may surface as the test corpus grows

## Clear next steps

1. **Fix non-explicit deduction from template-specialisation function parameter packs.**
   - `buildDeductionMapFromCallArgs` currently skips parameter-pack slots in its template-argument-matching pass (gated on `func_param_to_call_arg_index[i] == SIZE_MAX`). The fix is to iterate over each call-arg in the pack's slice and run the template-arg pairwise matching for the parameter-pack element type, writing each deduced inner type into `param_name_to_arg` for the variadic template parameter.
   - Example gap: `template<typename... Ts> int f(Box<Ts>... boxes)` called as `f(a, b)` should deduce `Ts = {int, double}` if `a` is `Box<int>` and `b` is `Box<double>`. Currently deduces `Ts = {Box<int>, Box<double>}` instead.

2. **Keep the deduction regression cluster close.**
   - `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
   - `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
   - `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
   - `tests/test_pack_nonpack_mixed_explicit_deduction_ret0.cpp`
   - `tests/test_explicit_template_pack_sizeof_param_name_ret0.cpp`

## Recommended interpretation of the roadmap

If you want the shortest accurate summary:

- **Done:** Phases 1-4
- **Done:** Phase 5 materialization / ownership cleanup
- **In progress:** Phase 6 explicit-deduction cleanup
- **Landed in Phase 6 so far:** the nondeduced-tail positional-fallback fix, canonical function-parameter → call-argument pack-slice metadata, pack-before-tail explicit deduction, mixed explicit + deduced pack support, multi-pack sizeof resolution, fold expressions with complex pack operands
- **Work left after this PR:** non-explicit (implicit) deduction of inner template types from `Box<Ts>...`-style function parameters; the roadmap is **not** fully exhausted yet, so this document should stay

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
- `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
- `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
- `tests/test_pack_nonpack_mixed_explicit_deduction_ret0.cpp`
- `tests/test_explicit_template_pack_sizeof_param_name_ret0.cpp`
- `tests/test_fold_expr_complex_pack_ret0.cpp`

## Validation baseline refreshed on 2026-04-23 (Phase 6: fold-expression complex-pack parser fix)

Linux validation run after:
- Extending fold-expression Pattern 1 `(... op cast-expr)` to accept non-bare-identifier pack operands in the parser.
- Extending fold-expression Pattern 3 `(init op ... op cast-expr)` similarly.
- Adding `FoldExpressionNode` constructor for binary fold with complex pack expression.
- Adding regression test `tests/test_fold_expr_complex_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2183** compile+link+runtime passing tests (+1 from new `test_fold_expr_complex_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

Landed on top of the drain-by-ODR-use pass:
- `SemanticAnalysis::drainLazyMemberRegistry` now calls `normalizeTopLevelNode(*result)` immediately after each successful `ensureMemberFunctionMaterialized` (in both the AST-walk pass and the ODR-use pass). This guarantees a freshly-substituted body has its internal call expressions annotated by sema (routed through `tryAnnotateCallArgConversions` → `tryMaterializeLazyCallTarget`). The normalize helpers dedup via `normalized_bodies_`, so re-normalizing an already-processed node is a safe no-op.

Deeper diagnosis of the residual 4 tests / 10 forwarder hits (unchanged count after the re-normalization change):
- The hits are always calls inside a freshly-materialized body (e.g. `helper()` / `other.method(...)` inside `Box<int>::compute()`).
- Instrumenting `tryMaterializeLazyCallTarget` reveals the real cause: when sema annotates those inner calls, it resolves them to the **template pattern's** declaration (`Box::helper`, `Box::method`), **not** to the instantiated struct's lazy stub (`Box$3ee5c699332008a6::helper`). The template pattern has its body, so `tryMaterializeLazyCallTarget` correctly short-circuits (`func_decl->get_definition().has_value() == true`). No lazy materialization is triggered at sema time, so the codegen forwarder is legitimately the first materializer when codegen lowers the instantiated receiver.
- This is a **substitution-layer gap**, not a materialization-layer gap: `ExpressionSubstitutor` does not rewrite intra-struct call targets to point at the instantiated struct's stubs. Fixing this properly means teaching the substitutor to redirect any `CallExprNode` whose resolved target is a member of the owning template pattern to the corresponding member of the instantiated struct (before sema runs), or alternatively teaching `tryMaterializeLazyCallTarget` to map a pattern-resolved call to the active instantiation via `member_context_stack_`.
- Scope of that fix is significantly larger than Slice G and deserves a separate slice (call it Slice H: "intra-instantiation call-target rewriting"). Until then, the codegen forwarder remains the correct and minimal bridge for these 10 hits.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G: ODR-use drain extension + audit log)

Landed on top of the Slice G foundation:
- `LazyMemberInstantiationRegistry::snapshotOdrUsedLazyEntries()` — returns a snapshot of `(owner, member, is_const)` triples for lazy entries currently marked ODR-used. Snapshot (not live view) because materialization mutates the map.
- `SemanticAnalysis::drainLazyMemberRegistry` gained a second fixpoint pass after the AST-walk pass that materializes the ODR-used residuals via `ensureMemberFunctionMaterialized`. Safe against SFINAE-probed instantiations by construction: they never flow through a sema annotation site that calls `markOdrUsed`.
- `AstToIr::materializeLazyMemberIfNeeded` gained a debug-level audit log (`Codegen:Debug`) that fires when the forwarder is still the first materializer for a lazy member.

Audit results (run against the full test corpus with `--log-level=Codegen:debug`):
- **4 tests, 10 first-materializer hits remain.** All hits are inner calls inside freshly-materialized bodies (e.g. calls to `method` / `helper` inside `Box<int>::compute()`'s body).
- Root cause: `drainLazyMemberRegistry` runs *after* `normalizePendingSemanticRoots`, so the freshly-substituted bodies created by the drain never get their internal `tryAnnotateCallArgConversions` pass. That means calls inside those bodies never reach `tryMaterializeLazyCallTarget`, never get `markOdrUsed`, and are left for the codegen-side forwarder to resolve on demand.
- This is the clear next architectural step: interleave sema annotation with the drain (either run `normalizePendingSemanticRoots` after the drain, or loop sema+drain to a fixpoint) so that newly-substituted bodies also get annotated before codegen runs. That would either eliminate the forwarder hits entirely or narrow them to an even smaller residual worth diagnosing.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline (unchanged by the drain extension):

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G foundation: explicit ODR-use plumbing)

Windows validation was re-run after landing:
- `LazyMemberInstantiationRegistry::markOdrUsed` / `isOdrUsed` / `odr_used_` set (with `...Any` variants and `clear()` integration) in `src/TemplateRegistry_Lazy.h`.
- `markOdrUsed` calls at three non-speculative sema sites:
  - `structHasConversionOperatorTo` in `src/SemanticAnalysis.cpp` (Slice A / tryAnnotateConversion path).
  - `tryMaterializeLazyCallTarget` in `src/SemanticAnalysis.cpp` (Slice B/C / direct + indirect call target).
  - `ensureSelectedConstructorMaterialized` in `src/SemanticAnalysis.cpp` (ctor selection).
- `ensureMemberFunctionMaterialized` intentionally left neutral — it is reachable from codegen / constexpr forwarders whose semantics are weaker than "sema proved ODR-use".

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

The foundation is in place for the drain-by-ODR-use extension to land as a separate follow-up commit (see step 2 under "Clear next steps").

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

## PR #1344 review comments addressed (2026-04-23)

Four review comments from PR #1344 were addressed:

### Fix 1: Tab/space indentation (Devin)
`src/Parser_Templates_Inst_Deduction.cpp` lines 1006-1012 had mixed leading
space + tab characters.  Replaced with pure tab indentation to match the rest
of the file.

### Fix 2: Namespace/template-specialisation pack name extraction (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — the pack-parameter name extractor
used `fp_type.token().handle()` unconditionally.  For simple `Ts... args` that
token correctly returns `Ts`, but for `MyBox<Ts>... args` it returns `MyBox`
(the template name), not the pack parameter `Ts`.

The fix: after reading the token handle, check whether it exists in
`tparam_nodes_by_name`.  If it does not (or is invalid), walk the `TypeInfo`
for the type specifier; if the type is a template instantiation, iterate its
`templateArgs()` and use the first entry whose `dependent_name` is valid as the
pack parameter name.  The stale `FLASH_LOG_FORMAT(Templates, Error, ...)`
diagnostic that fired for every template-instantiation type was removed because
it incorrectly flagged valid cases as errors.

A new test `tests/test_explicit_pack_template_specialization_ret0.cpp` was
added that calls `f<0>(a, b)` where `f` is declared
`template<int N, typename... Ts> int f(Box<Ts>... boxes)`.

### Fix 3: ScopeGuard ordering for exception safety (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — `ScopeGuard
restore_template_pack_sizes` was constructed *after* the loop that populates
`template_param_pack_sizes_`.  If the loop threw (e.g., `std::bad_alloc`) the
saved data would be permanently lost.  Moved the `ScopeGuard` construction to
immediately after the `std::move` and before the population loop.

### Fix 4: Empty pack falling through to naive fallback (Devin)
`src/Parser_Templates_Substitution.cpp` — when
`get_template_param_pack_size` returned `0` for an authoritative empty pack,
`found_variadic` was set to `true` but `num_pack_elements` stayed `0`, causing
the next `if (num_pack_elements == 0)` guard to re-enter the naive
`template_args.size() - non_variadic_count` formula.  This overcounts when
multiple variadic packs are present.  Changed the guard condition to
`if (num_pack_elements == 0 && !found_variadic)` so that an authoritative
empty-pack result is respected and the naive fallback is skipped.

### Validation baseline (2026-04-23)

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2182** compile+link+runtime passing tests (includes new test)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**
