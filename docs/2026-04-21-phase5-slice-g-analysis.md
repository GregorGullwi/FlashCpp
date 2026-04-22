# Phase 5 Slice G — Current status and next steps

This doc is now a short follow-up to `docs/2026-04-08-template-instantiation-materialization-plan.md`.
It keeps only the Slice G results that still matter for the unfinished Phase 5 / Phase 6 work.

## What is done and still relevant

- **Lazy-member ownership moved to sema.** The ODR-use plumbing, sema-owned drain, and codegen-bridge cleanup are landed and no longer the active work for Phase 5.
- **Parser-side instantiation mode exists.** `TemplateInstantiationMode` (`HardUse`, `SfinaeProbe`, `ShapeOnly`) replaced `in_sfinae_context_` as the parser-side authority.
- **ShapeOnly commit suppression landed.** Declaration-time default-template-argument parsing can instantiate signatures without publishing the commit-time side effects that should only happen for real use.
- **Function-body failure memoization landed.** Post-emplacement body-reparse failures now mark the function node as failed substitution and also keep the registry-level per-overload memo.
- **ShapeOnly cached class-template upgrades are now covered.** A specialization first seen in `ShapeOnly` mode can later upgrade to a real full instantiation instead of getting stuck as a signature-only cached type. Guardrail: `tests/test_shapeonly_cached_class_upgrade_ret0.cpp`.

## What is still open

### 1. Class-template shape/body state is still only partially explicit

Function instantiations now have a real three-state body model (`NotMaterialized`, `Materialized`, `FailedSubstitution`), but class-template instantiation still relies on a lighter `shape_only` marker plus a handful of upgrade checks at instantiation boundaries.

That is good enough for the current fixes, but it is not yet the “single explicit materialization state machine” envisioned in the original plan.

### 2. There is still no first-class threaded `InstantiationContext`

The parser now has the right **mode**, but not the full context object described in the original Slice G proposal:

- no origin/provenance payload
- no parent-chain for instantiation backtraces
- no single object threaded uniformly through class/function/lazy instantiation APIs

This is the main remaining architectural gap behind Slice G item #3.

### 3. `ShapeOnly` semantic policy is not fully documented yet

Some former `in_sfinae_context_` read sites now intentionally treat `ShapeOnly` as a non-hard-use mode.
That behavior is currently relied on by declaration-time default-template-argument parsing, but the rule is still implicit in code review discussions rather than written down as a crisp policy.

## Clear next steps

1. **Finish the class-template upgrade model.**
   - Replace the scattered “cached ShapeOnly specialization needs full upgrade” checks with a more explicit helper or state transition.
   - Make the primary-template, filled-defaults, and specialization paths share the same upgrade rule.

2. **Write down the `ShapeOnly` contract.**
   - Decide which parser soft-failure sites should treat `ShapeOnly` like `SfinaeProbe`, and which should stay hard errors.
   - Add/update regression tests around declaration-time default-template-argument parsing once that rule is explicit.

3. **Introduce a real `InstantiationContext` object if Slice G is to be considered complete.**
   - Minimum useful payload: mode, origin site, and parent context.
   - Thread it through class-template instantiation, function-template instantiation, and lazy member materialization.

4. **After Phase 5 is stable, continue the separate Phase 6 audit.**
   - The remaining explicit-deduction cleanup from `docs/2026-04-08-template-instantiation-materialization-plan.md` is still open, but it should stay separate from the Phase 5 materialization/state work.

## Guardrail tests for this area

- `tests/test_namespaced_pair_swap_sfinae_ret0.cpp`
- `tests/test_shapeonly_cached_class_upgrade_ret0.cpp`
- `tests/test_template_template_forward_decl_definition_ret0.cpp`
- `tests/test_template_template_body_reparse_odr_use_ret0.cpp`
