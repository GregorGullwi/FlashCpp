# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-28

This document tracks the current state of template instantiation, lazy
materialization, and the sema/codegen boundary. It is intentionally compact:
completed implementation history is summarized, and active work points at the
remaining concrete compiler gaps.

## Current State

The original template materialization plan is mostly closed.

- **Phases 0-4 are complete.** The original regression cluster remains covered;
  non-type template-argument identity has a canonical key path; alias-template
  materialization is centralized; late-materialized roots have an explicit
  register/normalize lifecycle; and dependent placeholder state is represented
  through `DependentPlaceholderKind`.
- **Phase 5 is complete in its intended ownership direction.** Sema now owns the
  first materialization of lazy member functions for the exercised ODR-use paths.
  Constructor, conversion-operator, direct-call, indirect-call, member-access,
  and struct-visitor slices were moved away from codegen-first materialization.
  Codegen's remaining lazy-member bridge delegates back to sema and is treated as
  a migration/diagnostic surface, not the desired first materializer.
- **Phase 6's concrete pack-deduction and pack-expansion reproducers are fixed.**
  Explicit and implicit function-template pack deduction now handles pack-before
  tail signatures, mixed explicit+deduced slices, multiple packs, packs not
  mapped to a function parameter pack, template-specialization element types
  such as `Box<Ts>...`, multi-dependent element types such as `Pair<Ts, Us>...`,
  nested wrappers such as `Wrap<Pair<Ts, Us>>...`, and class-template member or
  static-member function template cases. Fold-expression parsing/substitution
  now handles complex pack operands, and brace-initializer pack expansion works
  for explicit-size arrays, unsized arrays, and aggregate struct initialization.
- **The parser/template/sema boundary is substantially stronger.** The
  sema-owned post-parse surface rejects surviving `FoldExpressionNode` and
  `PackExpansionExprNode` forms; `SemanticAnalysis.cpp` no longer depends on
  direct `parser_.get_expression_type(...)` fallbacks; codegen fold/pack
  handlers are assertion-only; and late materialization has a pending
  semantic-root handoff.
- **`docs/KNOWN_ISSUES.md` no longer tracks template-pack materialization
  issues.** It currently records an unrelated floating-point array-subscript
  codegen bug.

## Recommended Next Steps

1. Pick one active fallback class from `docs/2026-04-27-fallback-comments-audit.md`
   and add a hard-fail probe or focused regression before changing behavior.
2. Prefer root-fixing metadata loss over deleting a fallback directly. Several
   probed fallbacks are active in the current corpus.
3. When a fallback is proven dead or root-fixed, delete the fallback and compact
   the audit note instead of adding another historical section here.
4. Validate template/compiler-source changes with `.\build_flashcpp.bat` and
   `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`.

## Landed Coverage

The following tests are representative guardrails for the now-closed template
materialization and pack-deduction work:

- `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
- `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
- `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp`
- `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`
- `tests/test_multi_pack_sizeof_deduction_ret0.cpp`
- `tests/test_implicit_deduction_box_pack_ret0.cpp`
- `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`
- `tests/test_multi_dep_pack_ret0.cpp`
- `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`
- `tests/test_nested_multi_dep_pack_ret0.cpp`
- `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`
- `tests/test_class_template_nested_pack_member_ret0.cpp`
- `tests/test_class_template_inner_func_pack_fold_ret0.cpp`
- `tests/test_class_template_static_member_func_template_ret0.cpp`
- `tests/test_fold_expr_complex_pack_ret0.cpp`
- `tests/test_fold_member_access_ret42.cpp`
- `tests/test_pack_expansion_brace_init_ret42.cpp`
- `tests/test_unsized_array_pack_expansion_ret3.cpp`
- `tests/test_aggregate_struct_pack_expansion_ret42.cpp`
- `tests/test_late_member_body_member_template_ret42.cpp`
- `tests/test_pending_sema_normalization_ret0.cpp`
- `tests/test_static_member_pack_expansion_boundary_ret0.cpp`

Do not reopen the old Phase 5/6 checklist unless a new reproducer shows a
specific regression. New work should start from the recommended next steps
above, then use the active roadmap below for context.

## Active Roadmap

The remaining template-instantiation risk is no longer one narrow pack
deduction bug. It is the broader fallback/substitution architecture documented
and probed in `docs/2026-04-27-fallback-comments-audit.md`.

### 1. Authoritative Substitution Context

Template instantiation still reconstructs dependent facts after primary
substitution loses them. The target is one substitution context that carries:

- type template parameters
- non-type template parameter values and categories
- pack slices, pack sizes, and co-pack relationships
- current-instantiation identity
- namespace/member context
- SFINAE/error-mode state

Active fallback evidence from the 2026-04-27 audit:

- unresolved class-template type arguments still pass through as original
  `TypeSpecifierNode`s for deferred-base and dependent-member cases;
- deferred `decltype(...)` base evaluation now materializes placeholder
  template-instantiation `TypeInfo` through
  `materializeDeferredBasePlaceholderIfNeeded(...)` before registering the
  base, so that branch no longer preserves placeholder base results as final;
- unresolved template defaults still need catch-all handling for template
  template defaults and some NTTP defaults;
- class-template NTTP defaults now use the shared
  `substituteAndEvaluateNonTypeDefault(...)` path for final substituted
  evaluation, so ordinary `noexcept(...)` / `sizeof(T)`-style defaults no longer
  depend on a local context-free ConstExpr fallback;
- `type_index == 0` dependent-placeholder handling has been narrowed in
  template-argument classification: ordinary comparison speculation now fails
  the tentative template-argument parse, known current template parameters are
  materialized with canonical placeholder indices, and any remaining invalid
  placeholder in an active template context is an invariant failure;
- fold substitution for non-type parameter packs still reconstructs values from
  `template_params` / `template_args`;
- `sizeof...` still depends on preserved per-pack size metadata plus broader
  reconstruction paths after scope-local pack facts are dropped;
- lazy static-member initialization now preserves declaration AST plus saved
  initializer source on `StructStaticMember` and replays that source under the
  instantiated template scope first; general `ExpressionSubstitutor`,
  call-target rebinding, and dependency pre-instantiation still remain for
  residual no-source / specialized-expression cases, while the late
  rebinding/dependency/evaluation work stays as the unconditional
  post-substitution normalization step and dependency discovery uses the shared
  AST traversal helper;
- function-template declaration reparse now runs whenever saved declaration
  source exists, including abbreviated/constrained forms and function-try-block
  cases; the `T*` namespace-qualified call residual was fixed by consuming the
  standard pointer/reference declarator helper during return-type reparse.
  Abbreviated function templates now preserve that saved declaration start when
  the parser synthesizes the implicit template wrapper, so their instantiations
  stay on the declaration-reparse path instead of falling back to synthesized
  return-type reconstruction. Signature synthesis remains only for
  instantiations without saved declaration source. Function-template
  instantiations without saved body positions are now split: declaration-only
  instantiations are accepted, but real definitions without saved body
  positions hard-fail instead of copying body pointers.

Removal direction: make template bindings and instantiated `TypeInfo` metadata
authoritative at creation time, then replace name-based and positional recovery
with hard preconditions or targeted diagnostics.

### 2. Sema/Codegen Contract Hardening

Sema now owns much more expression typing and late materialization than it did
when this plan started, but codegen still contains recovery paths for missing
semantic facts. The target invariant is:

> Every codegen-visible expression, call, constructor call, conversion, and
> ODR-used lazy member has a sema-selected target/type/conversion before IR
> lowering starts.

Remaining work:

- make selected call targets and constructor/operator overloads mandatory sema
  output for normalized bodies, including late-instantiated bodies;
- require sema annotations for implicit conversions, binary arithmetic
  conversions, ternary branch conversions, contextual `bool`, initialization
  conversions, and user-defined conversions;
- keep the lazy-member forwarder only while it exposes missing sema coverage,
  then replace it with an invariant check;
- add hard-fail probes or debug guards around codegen fallback sites before
  deleting them.

### 3. Late Materialization Fixpoint

The pending semantic-root queue exists and several late materialization paths
already drain it. The next step is to make the invariant explicit and complete:

- newly materialized lazy member/static-member/class-template roots must be
  sema-normalized before codegen or constexpr consumers reuse them;
- parser-owned constexpr evaluation should consume sema-normalized AST and
  metadata rather than instantiating or repairing declarations on demand;
- materialization should either run to a pre-codegen fixpoint or normalize
  incrementally on demand before each downstream use.

The lower-risk migration path remains incremental on-demand sema normalization.
A full eager pre-codegen drain can be considered later if it simplifies the
pipeline without instantiating SFINAE-only or otherwise-unused lazy bodies.

### 4. Type/Layout Metadata Cleanup

Template work still intersects with type-size and type-identity fallbacks.
`fallback_size_bits_`, parser-stored `TypeSpecifierNode` sizes, alias metadata,
and `StructTypeInfo`/`EnumTypeInfo` layout data are not yet behind one complete
layout authority.

Removal direction:

- centralize complete-object layout queries behind a canonical `TypeIndex`-based
  service;
- distinguish complete, dependent, incomplete, alias-to-complete, and invalid
  layout states explicitly;
- reject invalid `sizeof` / array-bound / member-layout uses instead of falling
  through to zero, parser snapshots, or string-prefix type searches.

### 5. Parser Recovery Split

Some parser paths still skip unsupported or partially parsed template/member
constructs so later phases can recover. Valid C++20 constructs should be parsed
into explicit AST with enough metadata for sema; unsupported constructs should
produce targeted `CompileError`s unless a deliberate system-header recovery mode
is active.

## Latest Known Validation Baselines

Recent historical baselines recorded for this work:

- 2026-04-20 Linux/clang boundary work: full suite passed, 2167 files.
- 2026-04-23 Linux/clang nested wrapped-pack work: 2201 passing tests, 149
  expected-fail tests.
- 2026-04-27 Windows fallback audit probes: `.\build_flashcpp.bat` and
  `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
  were used to classify active/dead fallback paths.

- 2026-04-28 Windows fallback follow-up: full suite passed, 2268 regular tests
  and 154 expected-fail tests.

Refresh this section only after a new full validation run. Do not treat the
older pass counts as today's baseline without rerunning the suite.

## Related Documents

- `docs/2026-04-27-fallback-comments-audit.md`
- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`
