# Constexpr Evaluator Refactor Plan (Forward-Looking)

This note is primarily **forward-looking**. It defines the next cleanup/refactor steps around identifier/function/member resolution, while separating what should remain local to constexpr evaluation from what could become broadly reusable across the compiler. A short progress addendum is included so the remaining work stays grounded in what has already landed.

## Scope Boundary: C++20 Constexpr Only

This refactor plan is about cleanup and correctness for **C++20 constexpr**
evaluation only.

Explicit non-goals for this plan:

- no post-C++20 constexpr feature expansion
- no constexpr exception handling work (`throw`, `try`, `catch` during constant evaluation)
- no reinterpretation of internal evaluator error handling as language support

If the evaluator internally catches exceptions or converts failures into
`EvalResult::error(...)`, that is an implementation detail / diagnostics gap,
not support for constexpr exceptions. Those remain unsupported and are tracked
in the constexpr limitation docs.

## Progress Update (2026-03-08)

### Landed checkpoint commits

- `eb0983bf` — shared constexpr expression dispatch for bound-expression recursion
- `0fb14c95` — refactored shared constexpr lookup resolution helpers
- `e444335a` — extracted constexpr member source resolution
- `ccab61ff` — deduplicated constexpr identifier lookup helpers
- `a7432001` — refactored constexpr function symbol lookup
- `2b4c4544` — extracted constexpr function-call template context handling
- `fd0ebc3e` — shared constexpr member-function candidate lookup
- `e0430f14` — deduplicated bound constexpr function calls
- `e3b700ba` — shared constexpr parameter binding
- `5e3a7fb7` — shared callable operator lookup
- `431e1962` — reused constexpr constructor member bindings
- `a311908e` — shared constexpr single-return body evaluation
- `14dae6af` — shared constexpr pre-evaluated ctor bindings
- `46d27415` — shared constexpr ctor member initialization
- `6b4b1255` — shared constexpr ctor member lookup
- `3de8b202` — split constexpr member function lookup modes
- `98c919eb` — shared current constexpr member function lookup
- `a02435c4` — shared current struct static lookup gate
- `48ea4682` — shared constexpr static member default tail

### What is now complete

- shared expression dispatch helper centralizes common bound-expression walking logic
- shared lookup helpers cover `resolved_name`/raw-name fallback and parser-stored function targets
- current-struct static-member lookup/preference is centralized
- current-struct/member-function candidate filtering is centralized for the migrated call paths
- constexpr member-source extraction from object initializers is centralized for aggregate/designated/default-member/constructor-member cases
- repeated template-binding save/restore logic around constexpr function-call evaluation is centralized
- duplicated bound-expression function-call recursion branches are centralized through one helper
- evaluated-argument parameter binding is centralized across regular function calls, lambda calls, callable objects, and member-function calls
- callable `operator()` candidate scanning is centralized for the migrated callable-object paths
- constructor/member extraction now reuses shared evaluated-argument binding where the callsites match exactly
- repeated single-return block-body evaluation is centralized for callable objects, lambda block bodies, and member functions
- pre-evaluated constructor-argument binding is centralized for the remaining nested and array-element member-resolution paths
- constructor member-initializer application plus default-member fallback is centralized for the migrated callable-object and ctor-backed object extraction paths
- single-target ctor member lookup with default-member fallback is centralized for the migrated nested and array-element member access paths
- member-function candidate lookup now supports lookup-only vs constexpr-evaluable filtering without changing caller error behavior
- lazy-instantiated current-struct plus base-template member-function lookup is centralized for the migrated function-call and member-function-call paths
- current-struct static lookup mode gating plus identifier-name-handle normalization is centralized
- the repeated static-member "evaluate initializer or synthesize scalar default" tail is centralized for the migrated instance-access paths
- static-member lookup from struct type info now uses `findStaticMemberRecursive` for consistent base class support
- extracted `extract_identifier_from_expression` helper to deduplicate object expression parsing in member function call and object member extraction

### Early remaining follow-up seams

- the obvious ctor/member-resolution micro-duplication is largely gone; remaining cleanup candidates are smaller and need re-audit before extraction
- the main open question is now whether any lookup-only helper has earned promotion out of `ConstExprEvaluator`
- remaining static-member duplication is now down to more behavior-specific paths (lazy instantiation, qualified fallback lookup, and array extraction)

### Remaining likely steps after that

- re-audit `evaluate_member_function_call`, static-member lookup, and nearby member-call paths for any remaining local duplication that still mixes lookup and evaluation
- decide whether the current lookup-only helpers should remain constexpr-local or be promoted only once a second non-constexpr consumer exists
- decide whether any lookup-only helper has earned promotion into a broader semantic utility, or should remain constexpr-local for now

## Make-This-Implementable Checklist

Before starting the next slice, build a tiny audit table with one row per
remaining candidate site:

- function / area
- duplication kind (`lookup-only` vs `evaluation-coupled`)
- likely helper boundary
- whether it depends on `EvalResult`
- whether a non-constexpr consumer already exists

That turns the next pass into a bounded extraction task instead of a vague
re-audit. If a site depends on `EvalResult`, keep it in the constexpr-local
track immediately instead of debating promotion.

## Initial Remaining Candidate Site Table (2026-03-11)

This is a **working inventory**, not a claim that every remaining duplication
site has been found. It is meant to make the next implementation slices small
and reviewable.

| Function / area | File | Duplication kind | Likely helper boundary | Depends on `EvalResult`? | Shared outside constexpr? |
| --- | --- | --- | --- | --- | --- |
| `evaluate_expression_with_bindings` + `evaluate_expression_with_bindings_const` | `src/ConstExprEvaluator_Members.cpp` | evaluation-coupled walker duplication | ~~shared dispatch layer for bound-expression recursion~~ **COMPLETED** | Yes | No, keep constexpr-local |
| `evaluate_qualified_identifier` | `src/ConstExprEvaluator_Members.cpp` | mixed lookup + constexpr synthesis | split qualified-type/static-member resolution from `integral_constant` / trait-value synthesis | Partly | Maybe later, but only the lookup half |
| `evaluate_member_access` | `src/ConstExprEvaluator_Members.cpp` | mixed object-source resolution + member evaluation tail | reuse a small helper for “resolve object, then either static-member fast path or member-source evaluation” | Yes | No |
| `evaluate_nested_member_access` | `src/ConstExprEvaluator_Members.cpp` | evaluation-coupled nested member extraction | helper for “evaluate final resolved member source” after `resolve_constexpr_member_source_from_initializer(...)` | Yes | No |
| `evaluate_member_function_call` | `src/ConstExprEvaluator_Members.cpp` | mixed lookup/evaluation dispatch | ~~isolate object-kind dispatch~~ **COMPLETED** - extracted `extract_identifier_from_expression` | Yes | No |
| `evaluate_function_call_member_access` + `evaluate_static_member_from_struct` | `src/ConstExprEvaluator_Members.cpp` | lookup-heavy static-member access | ~~lookup helper~~ **COMPLETED** - uses `findStaticMemberRecursive` | Partly | No |
| `evaluate_array_subscript_member_access` + `evaluate_variable_array_subscript` | `src/ConstExprEvaluator_Members.cpp` | evaluation-coupled array element extraction | ~~reviewed~~ - internal lambdas too specific | Yes | No |
| constructor-backed member extraction | `src/ConstExprEvaluator_Members.cpp` | evaluation-coupled constructor-member interpretation | ~~helper for ctor member binding~~ - already uses extracted helpers | Yes | No |

## Suggested Next Slice Order

1. ~~**`evaluate_expression_with_bindings*` pair**~~ ✅ COMPLETED
   - highest duplication density
   - fully constexpr-local
   - easiest to review in isolation
2. ~~**static-member lookup family**~~ ✅ COMPLETED
   - `evaluate_qualified_identifier` - tightly coupled with EvalResult, not extracted
   - `evaluate_function_call_member_access` - already uses helpers
   - `evaluate_static_member_from_struct` - refactored to use `findStaticMemberRecursive`
3. **member-access family**
   - `evaluate_member_access` - already uses extracted helpers
   - `evaluate_nested_member_access` - already uses extracted helpers
   - array-member extraction paths - internal lambdas too specific
4. **Review remaining candidates for additional extraction opportunities** - completed review, no further extraction opportunities identified
   - remaining static-member duplication is now down to more behavior-specific paths
   - constructor-backed member extraction already uses `try_evaluate_member_from_constructor_initializers`
   - array-element extraction has internal lambdas with specific return types

## Goals

- Reduce duplicated resolution and member-extraction logic in the constexpr evaluator.
- Make lookup behavior more consistent across constexpr consumers.
- Isolate **generic semantic resolution policy** from **constexpr-only value evaluation**.
- Keep changes incremental, testable, and easy to revert if a slice proves too behavior-sensitive.

## Non-Goals

- No parser redesign.
- No IR or codegen redesign.
- No changes to `src/IRTypes_Ops.h` as part of this plan.
- No broad architectural move unless a helper proves clearly reusable outside constexpr.
- No constexpr exception-handling feature work; keep this plan strictly within C++20 constexpr semantics.
- No move of constexpr evaluation behavior into parser-owned code.

## Key Design Split

The main architectural recommendation is to separate this work into two layers.

### Layer A: Generic declaration/target resolution

This layer answers questions like:

- Which declaration does this identifier refer to?
- Which function declaration does this `FunctionCallNode` target?
- Should current-struct static-member binding outrank raw global lookup here?

This layer should return things like:

- `ASTNode`
- declaration pointers
- symbol handles
- owner-struct metadata when needed

This is the part most likely to become reusable outside constexpr evaluation.

If promoted, these helpers should live in a **shared semantic-resolution
utility layer** usable by:

- the constexpr evaluator
- regular non-constexpr semantic/codegen consumers
- parser-adjacent semantic follow-up paths when needed

They should **not** become parser-owned evaluation helpers. The goal is to
share lookup policy, not to move constant-evaluation behavior into the parser.

### Layer B: Constexpr-only object/member evaluation

This layer answers questions like:

- Given a constexpr object initializer, what is the value of member `x`?
- If the object is aggregate-initialized, which initializer corresponds to member `x`?
- If the object is constructor-initialized, what parameter bindings are needed to evaluate member `x`?

This layer is tightly coupled to:

- `EvaluationContext`
- `EvalResult`
- aggregate/constructor interpretation
- default member initializers
- array/member element evaluation

This layer should stay in `ConstExprEvaluator` unless a clearly broader abstraction emerges.

## Recommended Refactor Tracks

## Track 1: Finish extracting generic constexpr lookup helpers

### Objective

Identify remaining duplicated declaration-resolution logic that does not need to know about `EvalResult` or initializer interpretation.

### Candidates

- current-struct static-member lookup preferences
- qualified/static-member fallback helpers
- declaration lookup for member-access-related consumers
- any repeated `resolved_name` / raw-name fallback sequence still left in constexpr code

### Success Criteria

- duplicated lookup ladders become single helpers
- helper interfaces return declarations/metadata, not evaluated values
- no behavior expansion beyond cleanup/consistency

## Track 2: Extract shared member-from-initializer logic inside constexpr

### Objective

Reduce duplication in the parts of constexpr evaluation that read a member value out of an already-resolved object initializer.

### Likely helper boundary

A helper in `ConstExprEvaluator` that takes roughly:

- object initializer
- declared type index or struct info
- target member name
- evaluation context

and returns either:

- the resolved member AST/value source, or
- an `EvalResult` directly if the evaluation step is inseparable

### Behavior it should centralize

- aggregate member lookup by position
- designated initializer lookup
- fallback to default member initializers
- constructor-call member lookup through existing constructor/member helpers
- constructor parameter binding when member expressions depend on ctor args

### Important constraint

Do **not** move this helper into a general semantic layer unless it stops depending on constexpr-specific data structures.

## Track 3: Decide whether to promote some lookup helpers into a shared semantic utility

### Objective

Determine whether the declaration-resolution helpers are useful enough outside constexpr to justify a shared home.

### Likely external consumers

- codegen paths that still duplicate declaration targeting rules
- semantic/template utilities that need the exact function target from parser metadata
- static member lookup logic currently open-coded in isolated places
- parser-adjacent semantic lookup paths that need the same declaration-resolution policy without constexpr evaluation

### Promotion target

If a helper graduates out of `ConstExprEvaluator`, prefer a small shared
semantic-resolution utility layer rather than placing it directly in parser
logic. That keeps the parser thin while still letting parser-adjacent semantic
work reuse the same lookup rules.

### Promotion rule

Only promote a helper if all of the following are true:

1. it returns declarations/lookup metadata rather than evaluated values
2. it does not depend on `EvalResult`
3. it does not depend on constructor/aggregate interpretation
4. at least one non-constexpr consumer needs the same policy

If those conditions are not met, keep the helper local to `ConstExprEvaluator`.

## Recommended Implementation Order

### Step 1

Audit remaining constexpr consumers for duplicated **lookup policy** only.

Deliverable:
- a short list of duplication sites with proposed helper boundaries

### Step 2

Extract any remaining declaration-resolution helpers that are still purely lookup-oriented.

Deliverable:
- small helper additions in `ConstExprEvaluator.h/.cpp`
- callsite simplification without behavior expansion

### Step 3

Extract a constexpr-local helper for **member lookup from initializer**.

Deliverable:
- one helper for aggregate/constructor/default-member paths
- one or two consumers migrated first

### Step 4

After early validation, migrate the remaining member/member-array consumers.

Deliverable:
- reduced duplication across member-oriented constexpr evaluation paths

### Step 5

Re-evaluate whether any lookup-only helper now belongs in a more general semantic utility.

Deliverable:
- either a follow-up extraction plan or an explicit decision to keep the helpers constexpr-local

## Validation Strategy

For each refactor slice:

1. build with `./build_flashcpp.bat`
2. run the smallest relevant focused tests first
3. run the nearby constexpr identifier/member/function regressions
4. only then run broader regression coverage if the slice changed shared logic

## High-Risk Areas

- default member initializer fallback
- designated aggregate initialization
- constructor-parameter-dependent member expressions
- current-struct static-member preference vs raw-name fallback
- function target resolution when parser metadata is incomplete
- interactions with lazy template/class instantiation

## Decision Rules During Refactoring

- If a helper needs `EvalResult`, keep it constexpr-local.
- If a helper needs only declarations/handles, consider broader reuse.
- If a refactor changes both lookup policy and evaluation behavior at once, split it.
- If a new helper forces awkward nullable state or special cases at every callsite, the boundary is probably wrong.

## Concrete Recommendation

The next best step is:

1. finish any remaining lookup-only deduplication inside `ConstExprEvaluator`
2. extract a **constexpr-local** member-from-initializer helper
3. defer any broader compiler-wide utility extraction until a second real consumer exists

This keeps the immediate cleanup useful and low-risk while preserving a clean path to wider reuse later.