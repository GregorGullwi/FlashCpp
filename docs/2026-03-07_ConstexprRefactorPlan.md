# Constexpr Evaluator Refactor Plan (Forward-Looking)

This note is intentionally **forward-looking only**. It does not restate already-landed fixes. The goal is to define the next cleanup/refactor steps around identifier/function/member resolution, while separating what should remain local to constexpr evaluation from what could become broadly reusable across the compiler.

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

## Progress Update (2026-03-08)

Completed slices:

- `resolve_constexpr_object_source(...)` now covers more member-oriented constexpr consumers, including plain member access, nested member access, member function calls, and member-array subscript.
- `evaluate_member_from_initializer(...)` now centralizes the constexpr-local member extraction path for aggregate/constructor/default-member cases in direct member access.
- aggregate-member scanning in nested aggregate member access and member-array subscript now reuses `find_aggregate_member_initializer(...)` instead of open-coded designated/positional loops.
- constructor-parameter binding for constructor-initialized constexpr objects now reuses shared constexpr-local helpers instead of repeating the same evaluate-and-bind loop at each member-oriented callsite.
- constructor-initialized single-member evaluation now reuses a shared constexpr-local helper for constructor member-initializer lookup plus default-member fallback in the remaining nested/array-member paths.
- full constructor-member materialization for constexpr member-function object extraction now reuses a shared constexpr-local helper instead of keeping a separate open-coded constructor/default-member binding ladder in `extract_object_members(...)`.

What remains before this plan is "done enough":

- re-audit the remaining lookup-only call/function-target helpers to see whether any are now clean enough for a broader semantic utility
- decide whether the remaining lookup-only helpers should stay inside `ConstExprEvaluator` or be promoted only after a second non-constexpr consumer appears
- document the explicit keep-local vs promote decision once a second non-constexpr consumer exists (or does not)

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
