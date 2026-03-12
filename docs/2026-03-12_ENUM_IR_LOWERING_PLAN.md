# Enum Lowering Plan: Keep Enum Identity in Semantics, Lower Runtime Representation Earlier

**Date**: 2026-03-12  
**Status**: Proposed  
**Context**: Follow-up design note after the enum-pointer `ExprResult` / slot-4 regressions

## Proposed Approach

The proposed design direction is:

- `enum` identity is primarily a **parser / semantic / type-system** concern
- the **runtime representation** of an enum is its underlying integral type
- therefore, carrying `Type::Enum` deeply into IR/codegen for ordinary arithmetic,
  loads/stores, pointer arithmetic, and ABI lowering is more fragile than helpful

The right design is **not** to: “erase enums immediately after
parsing and pretend they are just `int` everywhere”.  Enum **identity** still
matters after parsing for:

- overload resolution
- template matching / type traits
- mangling
- `typeid` / RTTI-like behavior
- `__underlying_type`
- diagnostics and semantic checking

The proposed approach is:

> keep enum identity in the semantic type layer, but lower enum **value
> representation** to the underlying integer type at the IR/codegen boundary
> wherever the backend only needs storage/ABI/arithmetic information.

This document lays out how to achieve this incrementally.

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

The recent `ExprResult` regressions demonstrate this issue:

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

## Target Design: Separate `IrType` Enum

The root cause of the enum-pointer regressions (and the `UserDefined` vs `Struct`
confusion) is that the same `Type` enum is used for both semantic identity and
runtime representation. Codegen helpers can always write `if (type == Type::Enum)`
and nothing prevents it — the bug class is structurally permitted.

The fix is to make illegal states unrepresentable: introduce a **separate enum
for IR-level types** that physically cannot express `Enum`, `UserDefined`,
`Auto`, or `Template`.

### Evidence that this already works

The backend (`src/IRConverter*.h`) already operates on an implicit reduced type
set:

- `Type::Enum` appears **zero times** in the backend
- `Type::UserDefined` appears **once** (exception handling guard)
- The backend discriminates on `Float` vs `Double` (SSE vs GPR), `Struct`
  (ABI classification for >64-bit values), and integer-of-size-N for everything
  else

The backend already doesn't care about `Enum` or `UserDefined`. Making that
constraint explicit just means the compiler enforces what is already the intent.

### The `IrType` enum

```cpp
// Runtime representation types — the only types that matter for
// register selection, ABI classification, and instruction emission.
enum class IrType : int_fast16_t {
    Void,
    Integer,        // all integer types, enums, bool — distinguished by size_in_bits + is_signed
    Float,          // 32-bit IEEE 754
    Double,         // 64-bit IEEE 754
    LongDouble,     // 80-bit x87 extended precision
    Struct,         // needs type_index for size/layout, ABI classification
    FunctionPointer,
    MemberFunctionPointer,
    MemberObjectPointer,
    Nullptr,
};
```

No `Enum`. No `UserDefined`. No `Char` vs `Int` vs `Long` (they are all
`Integer` with different `size_in_bits` and `is_signed`). No `Auto`, `Template`,
`Invalid` — those are parser/semantic concerns that must not leak into IR.

### Where the conversion happens

`AstToIr` is the converter. It already reads `TypeSpecifierNode` (which carries
the full `Type` enum) from AST nodes and populates `TypedValue` for IR
instructions. The conversion to `IrType` happens at exactly the points where
`TypedValue` is constructed — the same sites that currently do ad-hoc lowering.

### What it fixes

| Current bug class | Why `IrType` prevents it |
|---|---|
| Slot-4 `type_index` vs `pointer_depth` ambiguity | `IrType::Integer` with `pointer_depth > 0` is unambiguously a pointer — no heuristic needed |
| `getSizeInBytes` needing enum branches | `IrType::Integer` already carries `size_in_bits = 32` — no `gTypeInfo` lookup needed |
| Pointer stride recovery for enum pointees | `IrType::Integer` pointer with `size_in_bits = 32` → stride is 4, trivially |
| `UserDefined` vs `Struct` confusion | Both become `IrType::Struct` |
| Future contributors adding `if (type == Type::Enum)` in codegen | Won't compile — `IrType` has no `Enum` variant |

### `TypedValue` becomes the boundary

`TypedValue` (`src/IRTypes_Ops.h`) is the shared currency between codegen and
backend. Every IR op struct (`BinaryOp`, `AssignmentOp`, `CallOp`,
`DereferenceOp`, etc.) uses `TypedValue` for its operands. Changing
`TypedValue.type` from `Type` to `IrType` means codegen physically cannot leak
semantic-only type tags into the IR.

For the transition period, `TypedValue` can carry both fields:

```cpp
struct TypedValue {
    IrType ir_type;         // runtime representation (new, authoritative)
    int size_in_bits;
    IrValue value;
    bool is_signed;
    int pointer_depth;
    TypeIndex type_index;   // still needed for Struct layout / ABI
    // ... existing fields ...

    // Transitional: kept during migration, removed when backend is fully on ir_type
    Type semantic_type = Type::Invalid;
};
```

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

## Impact Analysis

### Files Likely to Require Modifications

The following files contain code that directly handles `Type::Enum` and will need review during implementation:

| File | Type of Changes Expected |
|------|-------------------------|
| `CodeGen_Expr_Primitives.cpp` | Enum-to-runtime lowering in primitive operations |
| `CodeGen_Expr_Conversions.cpp` | Conversion logic between enum and integral types |
| `CodeGen_Expr_Operators.cpp` | Binary/unary operator handling for enums |
| `CodeGen_Call_Direct.cpp` | Argument passing and return value handling |
| `IRConverter_Conv_CorePrivate.h` | IR conversion logic |
| `IROperandHelpers.h` | Operand creation helpers |
| `OverloadResolution.h` | Ranking and resolution logic |
| `NameMangling.h` | Type identity preservation |

### Components With Indirect Impact

These components use enum information but may not need direct changes:

- **Template instantiation** - Uses enum identity for matching; should be unaffected
- **Type traits** - `is_enum`, `__underlying_type` work at semantic layer; unchanged
- **Debug info** - Enum names come from semantic type; should be unaffected

### Expected Benefits

- Reduced fragility in codegen from explicit representation boundary
- Cleaner pointer arithmetic handling via explicit pointee-size helpers
- Simpler `ExprResult` encoding by reducing special-case enum handling
- Easier future maintenance when adding new nominal types

### Migration Complexity

The phased approach allows incremental migration:

- **Phase 0-1** (Inventory + Helpers): Low risk, adding new code only
- **Phase 2-3** (TypedValue refactoring): Medium risk, existing code paths modified
- **Phase 4-5** (Pointer semantics + ExprResult): Medium risk, core infrastructure changes
- **Phase 6-7** (ABI audit + Final cleanup): Lower risk, final adjustments

---

## Proposed First Slice

The proposed starting point is the narrowest, safest slice:

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

## Success Criteria

The enum lowering refactoring is considered complete when:

### Code Quality Metrics

- No `Type::Enum` branches in arithmetic, comparison, load/store, or assignment helpers in codegen
- Pointer operations use explicit pointee-size helpers rather than deriving size from enum metadata
- `ExprResult` encoding uses fewer special-case enum handling paths

### Functional Requirements

- All existing enum tests pass (arithmetic, comparisons, pointers, arrays)
- Overload resolution correctly prefers enum overloads over integer overloads
- Mangling for enum parameters remains unchanged
- `typeid` and RTTI-like features preserve enum identity
- `__underlying_type` and type traits work correctly

### Regression Prevention

- The original enum-pointer `ExprResult` regression (slot-4 encoding) does not recur
- Pointer arithmetic on enum pointers works correctly
- Enum values passed by value or reference follow correct ABI rules

### Cleanup Verification

- `Type::Enum` remains only in:
  - AST / type system layers
  - Semantic analysis
  - Mangling
  - Type traits evaluation
  - RTTI / type identity features
  - Diagnostics

### Test Coverage

| Test File | Coverage |
|-----------|----------|
| `test_enum_ret0.cpp` | Basic enum, enum class, explicit values, underlying type |
| `test_enum_class_mangling_ret0.cpp` | Mangling for enum class |
| `test_enum_implicit_conv_ret0.cpp` | Implicit conversion to int/unsigned/long |
| `test_enum_pointer_increment_ret30.cpp` | Pointer increment on enum pointers |
| `test_enum_pointer_array_subscript_ret0.cpp` | Array subscript on enum pointer arrays |
| `test_enum_scoped_selfref_ret0.cpp` | Scoped enum with self-referencing enumerators |
| `test_c_style_casts_ret65.cpp` | C-style casts with enums |
| `test_type_traits_intrinsics_ret147.cpp` | `__is_enum` trait |
| `test_underlying_type_ret42.cpp` | `__underlying_type` trait |
| `test_using_enum_ret6.cpp` | C++20 using enum |
| `test_switch_10_ret10.cpp` | Switch statements with enums |
| `test_enum_comparison_ret0.cpp` | Enum comparisons |
| `test_enum_bitwise_ret0.cpp` | Enum bitwise operations |
| `test_enum_increment_decrement_ret0.cpp` | Enum increment/decrement |

### Known Gaps

- Direct ++/-- on enum values
- Enum bitwise compound assignment (|=, &=, ^=)
- `enum class` not yet supported

### Verification Tests

The following tests should compile and return 0 when the enum lowering work is complete:

#### Enum Arithmetic Test

```cpp
enum Color : int { Red = 1, Green = 2, Blue = 3 };

int test_enum_plus_enum() {
    int c = Red + Green;  // Should be 3
    return c == 3 ? 0 : 1;
}

int test_enum_minus_enum() {
    int c = Blue - Red;  // Should be 2
    return c == 2 ? 0 : 2;
}
```

#### Enum Overload Resolution Test

```cpp
enum Color { Red = 1, Green = 2, Blue = 3 };

int overloaded(int x) { return 1; }
int overloaded(Color c) { return 2; }

int test_enum_preferred_over_int() {
    Color c = Green;
    // Should call overloaded(Color), not overloaded(int)
    return overloaded(c) == 2 ? 0 : 1;
}
```

#### Enum Class Test

```cpp
enum class Status { Pending = 0, Running = 1, Done = 2 };

int test_enum_class() {
    Status s = Status::Running;
    return s == Status::Running ? 0 : 1;
}
```

---

## Summary

This plan proposes:

- Keep enum identity in semantic/type metadata
- Lower enum runtime behavior to the underlying integral representation earlier
- Move the representation boundary earlier and make it explicit
