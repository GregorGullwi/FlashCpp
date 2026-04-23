# Phase 5 Slice G — Current status and next steps

This doc is now a short follow-up to `docs/2026-04-08-template-instantiation-materialization-plan.md`.
It keeps only the Slice G results that still matter for the unfinished Phase 5 / Phase 6 work.

**Last updated:** 2026-04-22 (steps 4–6 below landed; see "What is done" additions)

## What is done and still relevant

- **Lazy-member ownership moved to sema.** The ODR-use plumbing, sema-owned drain, and codegen-bridge cleanup are landed and no longer the active work for Phase 5.
- **Parser-side instantiation mode exists.** `TemplateInstantiationMode` (`HardUse`, `SfinaeProbe`, `ShapeOnly`) replaced `in_sfinae_context_` as the parser-side authority.
- **ShapeOnly commit suppression landed.** Declaration-time default-template-argument parsing can instantiate signatures without publishing the commit-time side effects that should only happen for real use.
- **Function-body failure memoization landed.** Post-emplacement body-reparse failures now mark the function node as failed substitution and also keep the registry-level per-overload memo.
- **ShapeOnly cached class-template upgrades are now covered.** A specialization first seen in `ShapeOnly` mode can later upgrade to a real full instantiation instead of getting stuck as a signature-only cached type. Guardrail: `tests/test_shapeonly_cached_class_upgrade_ret0.cpp`.
- **Class-template upgrade model unified (2026-04-22).** The scattered `cached_shape_only_needs_upgrade` lambda and its inline duplicate in the early-exit block have been replaced with a single static helper `cachedInstNeedsShapeOnlyUpgrade(const ASTNode*, bool)` shared by all four check sites (primary-template path, type-map early exit, filled-defaults path, and specialization path). State-transition commit points at both `mark_shape_only()`/`mark_materialized()` sites are now annotated with the explicit `NotMaterialized|ShapeOnly → ShapeOnly|Materialized` transition rule.
- **`ShapeOnly` contract written down (2026-04-22).** The `TemplateInstantiationMode` enum in `Parser.h` now carries a precise policy comment: when ShapeOnly is used, what it suppresses, how it differs from `SfinaeProbe` (not SFINAE — no soft failures; only commit-phase suppression), and its sticky propagation rule. `shouldCommitTemplateInstantiationArtifacts()` lists all artifact kinds; `selectTemplateInstantiationMode()` documents why ShapeOnly is sticky downward through nested instantiations.
- **`ParserInstantiationContext` threaded through instantiation APIs (2026-04-22).** A new `ParserInstantiationContext` struct (mode snapshot, `StringHandle origin_name`, `const ParserInstantiationContext* parent`) and its RAII guard `ScopedParserInstantiationContext` are defined in `Parser.h`. The guard is pushed at the entry of `try_instantiate_class_template`, `instantiateLazyMemberFunction`, and `try_instantiate_member_function_template`, giving every instantiation a provenance record and a traversable parent chain. A public `currentInstantiationContext()` accessor exposes the chain for diagnostic use. Note: `template_instantiation_mode_` remains the authoritative mode variable; the context carries a snapshot for backtrace purposes only.
- **`ParserInstantiationContext` used for diagnostics and backtraces (2026-04-22).** `ScopedParserInstantiationContext` now captures the instantiation backtrace into `g_parser_instantiation_notes` (a `thread_local std::string`) on first destructor invocation during stack unwinding (detected via `std::uncaught_exceptions() > 0`). Catch sites in `FlashCppMain.cpp` for the parse and sema phases print the notes alongside the error. `Parser::buildInstantiationNotes` walks the context chain and formats "note: in instantiation of '…' requested here" lines. Mode-only guards (SFINAE/ShapeOnly probes) use `StringHandle{}` so their frames are omitted from the backtrace.
- **`template_instantiation_mode_` unified with context stack (2026-04-22).** All 11 `ScopedState<TemplateInstantiationMode>` guard sites have been replaced with `ScopedParserInstantiationContext`. The guard now saves/restores `template_instantiation_mode_` in its constructor/destructor, making every mode change visible in the context chain. There are zero remaining `ScopedState.*template_instantiation_mode_` usages.
- **Phase 6 explicit-deduction audit complete (2026-04-22).** All deduction-related tests pass. The stale TODO comment in `try_instantiate_single_template` (claiming NTTP deduction was not supported) has been corrected: NTTPs are handled via the `is_value` path in `buildDeductionMapFromCallArgs`/`deduceTemplateArgsFromCall`. New stress test `tests/test_explicit_deduction_mixed_ret42.cpp` covers mixed explicit+deduced args, NTTP explicit + type deduced, and default template arg + explicit override.

## What is still open

### 1. Class-template shape/body state machine is not yet a single authority

The four-state `StructBodyStateTag` (`NotMaterialized`, `ShapeOnly`, `Materialized`, `FailedSubstitution`) is now consistently applied and committed via `cachedInstNeedsShapeOnlyUpgrade`, but the upgrade logic still lives inside `try_instantiate_class_template` as a re-entry pattern rather than a true state-machine transition helper on `StructDeclarationNode` itself. A future pass could promote the upgrade check into a `maybeUpgradeShapeOnlyToFull()` method on the node that encapsulates both the state guard and the log message.

## Clear next steps

1. ~~**Finish the class-template upgrade model.**~~ ✓ Done 2026-04-22.

2. ~~**Write down the `ShapeOnly` contract.**~~ ✓ Done 2026-04-22.

3. ~~**Introduce a real `InstantiationContext` object.**~~ ✓ Minimum viable version done 2026-04-22.

4. ~~**Use `ParserInstantiationContext` in error reporting.**~~ ✓ Done 2026-04-22. `g_parser_instantiation_notes` thread-local + `buildInstantiationNotes` + `ScopedParserInstantiationContext` destructor capture + catch-site printing.

5. ~~**Unify `template_instantiation_mode_` with the context stack.**~~ ✓ Done 2026-04-22. All 11 `ScopedState<TemplateInstantiationMode>` sites replaced with `ScopedParserInstantiationContext`.

6. ~~**After Phase 5 is stable, continue the separate Phase 6 audit.**~~ ✓ Done 2026-04-22. Explicit-deduction audit complete; stale TODO corrected; stress test added.

## Guardrail tests for this area

- `tests/test_namespaced_pair_swap_sfinae_ret0.cpp`
- `tests/test_shapeonly_cached_class_upgrade_ret0.cpp`
- `tests/test_template_template_forward_decl_definition_ret0.cpp`
- `tests/test_template_template_body_reparse_odr_use_ret0.cpp`
- `tests/test_explicit_deduction_mixed_ret42.cpp`

