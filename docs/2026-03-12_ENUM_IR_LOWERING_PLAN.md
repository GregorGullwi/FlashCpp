# Enum Lowering Plan: Keep Enum Identity in Semantics, Lower Runtime Representation Earlier

**Date**: 2026-03-12  
**Status**: Proposed  
**Context**: Follow-up design note after the enum-pointer `ExprResult` / slot-4 regressions

## Short Answer

I **mostly agree** with the direction in the question:

- `enum` identity is primarily a **parser / semantic / type-system** concern
- the **runtime representation** of an enum is its underlying integral type
- therefore, carrying `Type::Enum` deeply into IR/codegen for ordinary arithmetic,
  loads/stores, pointer arithmetic, and ABI lowering is more fragile than helpful

However, I do **not** think the right design is “erase enums immediately after
parsing and pretend they are just `int` everywhere”.  Enum **identity** still
matters after parsing for:

- overload resolution
- template matching / type traits
- mangling
- `typeid` / RTTI-like behavior
- `__underlying_type`
- diagnostics and semantic checking

So the better design is:

> keep enum identity in the semantic type layer, but lower enum **value
> representation** to the underlying integer type at the IR/codegen boundary
> wherever the backend only needs storage/ABI/arithmetic information.

This document lays out how to get there incrementally.

---

## Why the Current Design Is Fragile

The current compiler mixes two different concerns into a single `Type` flow:

1. **semantic identity**  
   “this is `Color`, distinct from `int` and distinct from another enum”

2. **runtime representation**  
   “this occupies 32 bits and uses integer arithmetic / integer ABI rules”

That conflation causes codegen-specific special cases like:

- `Type::Enum` branches in `generateIdentifierIr`
- `Type::Enum` branches in `toTypedValue(...)` / `toExprResult(...)`
- pointer arithmetic needing enum-specific size recovery
- enum-pointer metadata bugs around `type_index` vs `pointer_depth`
- codegen helpers needing to know when an enum should behave like an integer and
  when its nominal identity still has to be preserved

The recent `ExprResult` regressions are a good example:

- enum pointers were semantically “enum-flavored”
- but operationally they needed pointer-depth and pointee-size behavior identical
  to other pointer-like operands
- because `Type::Enum` survived too long in the codegen contract, the slot-4
  encoding/decoding logic had to keep guessing whether a payload meant
  `type_index` or `pointer_depth`

That is a symptom of the representation boundary being too late.

---

## What Should Still Know About Enums

These layers should continue to preserve enum identity:

### 1. Parsing / AST / Type System

Enums need distinct type identity in:

- `TypeSpecifierNode`
- `EnumTypeInfo`
- enumerator declarations
- underlying-type queries
- scoped/unscoped enum rules

### 2. Semantic Analysis

Enum identity matters for:

- overload resolution ranking
- implicit conversion checks
- template argument matching
- traits (`is_enum`, `is_scalar`, `__underlying_type`, etc.)
- distinct-type diagnostics
- constant evaluation of enumerators

### 3. Mangling / Type Identity / RTTI-ish Features

When a user writes a function taking `Color`, mangling and type identity must
still see `Color`, not merely `int`.

That means:

- mangling must preserve enum type identity
- any type metadata used by `typeid`, RTTI, or reflection-like features must
  preserve enum identity
- debug / diagnostic paths should still be able to print enum names

---

## What Should Usually *Not* Need `Type::Enum`

These backend/runtime-oriented concerns should usually operate on the enum’s
lowered representation instead:

- arithmetic on enum values after semantic validation
- comparisons
- loads and stores
- assignment
- unary inc/dec
- pointer arithmetic involving enum pointees
- ABI classification for passing / returning enum values
- register allocation / machine-width selection

In other words:

> once a semantic check has decided that an operation on an enum is legal, the
> emitted runtime operation should generally use the enum’s underlying integral
> representation.

---

## Target Design

Introduce an explicit split between:

- **semantic type**: the nominal type the user wrote
- **representation type**: the lowered runtime storage/ABI/arithmetic type

Conceptually:

```cpp
struct LoweredValueType {
    Type repr_type;          // e.g. Int / UnsignedInt / Long
    int repr_size_bits;
    int pointer_depth;

    // Optional semantic identity that still matters outside raw runtime ops.
    Type semantic_type;      // e.g. Enum
    TypeIndex semantic_type_index;
};
```

The exact structure can vary, but the key rule is:

- codegen/IR helpers that only care about runtime behavior consume the
  **representation**
- semantic / mangling / type-identity helpers consume the **semantic identity**

`ExprResult` is a natural place to make this cleaner, because it already
separates fields instead of forcing everything through a single overloaded
metadata slot.

---

## Proposed Migration Strategy

This should be done in small phases, not as a flag day rewrite.

### Phase 0 — Inventory and Guard Rails

Before changing behavior broadly:

- audit all `Type::Enum` checks in:
  - `CodeGen_Expr_Primitives.cpp`
  - `CodeGen_Expr_Conversions.cpp`
  - `CodeGen_Expr_Operators.cpp`
  - `CodeGen_Call_Direct.cpp`
  - `IRConverter_Conv_CorePrivate.h`
  - `IROperandHelpers.h`
  - `OverloadResolution.h`
  - `NameMangling.h`
  - trait evaluators / template registries
- classify each use as one of:
  - semantic identity
  - mangling / RTTI / trait behavior
  - runtime representation
  - compatibility shim / legacy tuple encoding

Add a short tracking table to this doc or a follow-up task doc.

### Phase 1 — Add Explicit Lowering Helpers

Add small helpers with a single definition of “enum runtime representation”:

```cpp
Type getRuntimeValueType(Type semantic_type, TypeIndex type_index);
int getRuntimeValueSizeBits(Type semantic_type, TypeIndex type_index, int semantic_size_bits);
bool preservesNominalIdentityInBackend(Type semantic_type);
```

For enums, these would map to the underlying integral type/size from
`EnumTypeInfo`.

Important:

- these helpers must not erase enum identity globally
- they only answer “what should IR/runtime operations use?”

### Phase 2 — Separate Semantic Metadata from Runtime TypedValue Construction

Today `TypedValue` often carries a single `type` that has to serve both semantic
and runtime needs.

Refactor the construction sites so that:

- arithmetic/comparison/load/store helpers build `TypedValue` from lowered
  runtime type
- enum nominal identity, when still needed, is carried separately through
  `type_index` / semantic metadata instead of reusing `type == Type::Enum`

Likely first targets:

- `toTypedValue(...)`
- `generateIdentifierIr(...)`
- `generateTypeConversion(...)`
- unary/binary operator helpers
- assignment helpers

### Phase 3 — Lower Enum Values Earlier in Expression Producers

For ordinary enum values (not enum-type queries, not mangling):

- lower `ExprResult.type` for operational values to the underlying integral type
- preserve enum identity separately when the caller still needs it

Examples:

- enumerator loads
- enum variables in arithmetic/comparison
- enum temporaries

This phase should sharply reduce enum-specific branches in codegen.

### Phase 4 — Make Pointer Semantics Representation-Driven

Pointer operations should derive from:

- pointer depth
- pointee representation size

not from ad hoc `Type::Enum` / `Type::Struct` special cases.

Introduce an explicit pointee-size helper for pointer-valued expressions:

```cpp
int getPointerPointeeSizeBits(const ExprResult& result, const ExpressionNode* source_expr = nullptr);
```

This is exactly the class of bug that caused the recent enum-pointer increment
regression.  The pointer stride should come from a proper pointee-size source,
not from re-deriving meaning from overloaded enum metadata.

### Phase 5 — Simplify `ExprResult` / Slot-4 Compatibility

Once enum runtime lowering is cleaner:

- fewer enum values will need special positional encoding
- `encoded_metadata` use should shrink further
- `toExprResult(...)` and `toTypedValue(...)` can become much more mechanical

This phase should be coordinated with the remaining `ExprResult` migration work.

### Phase 6 — ABI / IRConverter Audit

Check whether any backend lowering still needs `Type::Enum` specifically.

Candidates:

- argument passing classification
- return lowering
- integer vs floating register choice
- sign/zero-extension decisions

In many places the correct answer should be:

- use the lowered underlying integral representation
- keep nominal enum identity out of the backend fast path

### Phase 7 — Keep Enum Identity Only Where It Truly Belongs

After the lowering boundary is in place, `Type::Enum` should remain primarily in:

- AST / type system
- semantic analysis
- mangling
- type traits / `__underlying_type`
- RTTI / type identity features
- diagnostics

It should stop being a common runtime-operation discriminator in codegen.

---

## Non-Goals

This plan is **not** proposing:

- deleting `EnumTypeInfo`
- treating enums as interchangeable with integers in overload resolution
- mangling enum parameters as integers
- removing enum identity from diagnostics or traits
- a broad one-shot rewrite across the whole compiler

The goal is only to move the **representation boundary** earlier and make it
explicit.

---

## Risks

### 1. Over-eager lowering can break overload resolution semantics

If enum identity is erased too early, calls like:

```cpp
void f(int);
void f(Color);
```

will be misresolved.

So lowering must happen **after** semantic selection, not before.

### 2. Mangling and RTTI must keep nominal enum identity

Any path that affects symbol identity or runtime type identity must still
preserve enum-ness.

### 3. Pointer metadata needs a real representation model

Lowering enum values alone will not fix pointer issues unless pointee size is
also made explicit.  The recent `++(*pp)` regression shows this clearly.

### 4. UserDefined / typedef / alias paths have similar shape

This enum work will likely expose the same semantic-vs-representation split for
other nominal types.

---

## Concrete First Slice I Would Implement

If/when this plan is picked up, I would start with the narrowest, safest slice:

1. add `getRuntimeValueType(...)` and `getRuntimeValueSizeBits(...)`
2. add `getPointerPointeeSizeBits(...)`
3. convert unary/binary arithmetic helpers to use those helpers instead of
   branching directly on `Type::Enum`
4. keep enum identity preserved in `ExprResult` / semantic metadata only where
   a later consumer actually needs it
5. add focused tests for:
   - enum arithmetic
   - enum comparisons
   - enum pointers
   - enum arrays
   - overload resolution still preferring enum overloads over integer overloads
   - mangling for enum parameters unchanged

That would reduce codegen fragility without forcing a whole-compiler rewrite.

---

## Decision

I **do not agree** with the current design as an end state.

I **do agree** that enums should mostly stop being a runtime/codegen-level
special case, but only if we replace that with an explicit split:

- keep enum identity in semantic/type metadata
- lower enum runtime behavior to the underlying integral representation earlier

That is the design direction I would pursue.
