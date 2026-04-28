# Built-in Subscript + Implicit Pointer Conversion — Follow-up Plan

**Date:** 2026-04-27  
**Status:** Implemented in PR-1373, but follow-up architectural cleanup is still desirable

---

## What landed in PR-1373

PR-1373 fixed built-in subscript handling for class types with implicit conversion
operators to pointer types, including both:

- `pw[0]`
- `0[pw]`

when `pw` is a class such as:

```cpp
struct PtrWrapper {
	int data[4];
	operator int*() { return &data[0]; }
};
```

The shipped implementation now:

1. Detects pointer-producing conversion operators during semantic analysis.
2. Normalizes reversed built-in subscripts when the pointer operand is produced by such
   a conversion operator.
3. Infers the built-in subscript element type from the conversion operator result.
4. Annotates the array operand with a user-defined struct-to-pointer conversion.
5. Applies that conversion in codegen before built-in subscript address calculation.
6. Covers forward, reversed, const, struct-element, dereference, and template cases in
   regression tests.

This is standards-aligned for the supported scope and fixes the original bug.

---

## Evaluation of the current implementation

The current implementation is correct enough to ship, but it is not the best long-term
architecture.

### What is good about it

- It keeps the behavioural change narrowly scoped to built-in subscript handling.
- It reuses existing sema cast annotations and existing codegen conversion-operator call
  emission.
- It avoids changing overloaded `operator[]` resolution.
- It passed the full regression suite after landing.

### What is not ideal about it

The current implementation duplicates conversion-operator discovery across phases:

- semantic analysis has `findStructPointerConversionOperator`
- codegen has `findConversionOperatorForSubscriptPointerTarget`

That duplication means:

- base-class traversal policy must stay in sync in two places
- const-selection policy must stay in sync in two places
- exact target-type matching logic must stay in sync in two places
- lazy-member materialization and conversion lookup remain more coupled than necessary

It also required exposing a sema helper (`canonicalizeTypeForImplicitConversion`) mainly
so codegen could reproduce sema’s decision. That is a code smell: codegen is still
partially re-solving a semantic question that sema already answered.

---

## Recommended long-term direction

The better architecture is:

> **Semantic analysis should select the exact built-in subscript pointer conversion once,
> record that selection explicitly, and codegen should consume that decision without
> re-running conversion-operator lookup.**

In other words, sema should own *selection*, codegen should only own *execution*.

---

## Why this is better

Built-in subscript is semantically just:

```cpp
*((E1) + (E2))
```

If one operand is a class type, the hard question is not code generation. The hard
question is semantic selection of the implicit conversion sequence and of the exact
conversion operator that participates in that sequence.

Once sema has made that choice, codegen should not need to:

- walk the inheritance graph again
- re-check const viability
- re-check the return type shape
- reconstruct which conversion operator sema must have intended

That information should already be attached to the expression or the cast.

---

## Proposed follow-up refactor

### 1. Introduce sema-owned selected-conversion metadata

Extend the semantic cast representation used for `StandardConversionKind::UserDefined`
so it can carry the selected conversion function for struct-to-pointer conversions.

Preferred shape:

- store a stable pointer/reference to the selected `FunctionDeclarationNode`, or
- store a stable sema-owned lookup record that uniquely identifies the selected
  `StructMemberFunction`

This should follow the same spirit as how converting constructors are already tracked
explicitly instead of being re-resolved later.

### 2. Remove subscript-specific conversion-operator lookup from codegen

After sema records the selected conversion function, delete the dedicated codegen helper:

- `findConversionOperatorForSubscriptPointerTarget`

`generateArraySubscriptIr` should instead:

1. read the sema slot on `array_expr`
2. confirm the cast is a user-defined struct-to-pointer conversion
3. fetch the selected conversion function from sema metadata
4. call `emitConversionOperatorCall`

That makes codegen a pure consumer of sema decisions.

### 3. Unify the subscript path with the general user-defined conversion path

Today the built-in subscript support has some special-case logic because codegen needs
the converted pointer result before index arithmetic happens.

That special timing is fine, but the semantic model should still match the general
user-defined conversion machinery:

- sema determines the selected conversion
- codegen materializes it at the use site that needs the converted value

The subscript case should become “same model, different insertion point”, not “special
lookup path”.

### 4. Move helper logic away from pointer-only probing where practical

The current sema helper is specifically about pointer-returning conversion operators.
That was appropriate for getting the bug fixed quickly.

Long term, the more reusable abstraction is:

- find viable user-defined conversions from a source class type to a target canonical type

Then built-in subscript can simply ask:

- “is there a viable conversion to a pointer type?”

instead of using a bespoke pointer-conversion scanner.

That would make future work easier for:

- pointer conversions outside subscript contexts
- other built-in operators that rely on implicit conversion to pointer types
- future ambiguity diagnostics

### 5. Make ambiguity a first-class semantic diagnostic

The current implementation is intentionally narrow. A follow-up should explicitly
diagnose ambiguous pointer conversion operators in the built-in subscript path rather
than relying on whichever helper happens to find first.

This belongs in sema, not codegen.

---

## Suggested concrete work items

1. Extend `ImplicitCastInfo` (or an adjacent sema-owned side table) so a user-defined
   struct conversion can carry the exact selected conversion function.
2. Teach `tryAnnotateConversion` to record that selected conversion for struct-to-pointer
   cases.
3. Replace codegen’s subscript-specific conversion lookup with direct use of the sema
   selection.
4. Delete the now-redundant codegen helper and any bridging APIs that only existed to
   support duplicate lookup.
5. Add explicit tests for:
   - inherited conversion operators
   - const vs non-const conversion operator preference
   - ambiguous multiple pointer conversions
   - more than one pointer depth (`T**`) if that behaviour is intended to remain supported

---

## Scope that is still intentionally deferred

These areas were not required to fix the original bug and should remain separate
follow-up work:

- conversion operators returning array references (`operator T(&)[N]()`)
- ambiguity diagnostics for multiple viable pointer conversion operators
- broader unification of all user-defined conversion selection across sema/codegen
- non-subscript operators that may want to reuse the same infrastructure

---

## Bottom line

PR-1373 fixed the correctness bug and is acceptable as a shipping change.

However, the best long-term architecture is to move from:

- **“sema annotates, codegen re-discovers the exact conversion operator”**

to:

- **“sema selects the exact conversion operator, codegen only emits it”**

That should be the direction for any cleanup follow-up after this PR lands.
