# Subscript Operator + Implicit Pointer Conversion — Implementation Plan

**Date:** 2026-04-27  
**Status:** Deferred — not in PR-1372  
**Prerequisite:** PR-1372 merged (built-in reversed subscript normalization in sema)

---

## Background

PR-1372 normalizes built-in reversed subscripts (`0[array]`) in semantic analysis so that
`ArraySubscriptNode::array_expr()` always holds the pointer/array operand. It handles the
case where both operands are directly of pointer/array or integral types.

This document covers the deferred extension: supporting subscript expressions where the
"array" operand is a **class type with an implicit conversion operator to a pointer or
array type** — e.g. `0[pw]` and `pw[0]` where `PtrWrapper::operator int*()` exists.

Per C++20 [expr.sub]/1, `E1[E2]` is defined as `*((E1)+(E2))`, and the pointer/array
operand may result from an implicit conversion sequence. Both the reversed and forward
forms must work correctly.

### Current state (after PR-1372)

```cpp
struct PtrWrapper { int data[5]; operator int*() { return data; } };
PtrWrapper pw;
int a = pw[0];    // Compiles but produces WRONG result (conversion not applied in codegen)
int b = 0[pw];    // Compile error: "Operator+ not defined for operand types"
```

Clang compiles and runs both correctly. The forward case (`pw[0]`) is therefore a
**pre-existing bug** that this plan must also fix.

---

## Why this is architecturally distinct

The normalized AST after PR-1372 for `0[pw]` (once this feature exists) would be
`ArraySubscriptNode(pw, 0)`. At that point `array_expr()` is a **struct**, not a pointer.
Three separate compiler phases must then handle this:

1. **Sema – normalization detection** (`normalizeBuiltinSubscriptOperands`)
2. **Sema – conversion annotation** (`tryAnnotateConversion` / new path)
3. **Codegen – subscript IR generation** (`generateArraySubscriptIr`)

Currently none of the three has the required support, making this a multi-layer change.

---

## Required changes

### 1. New sema helper: `structHasPointerConversionOperator`

`is_array_or_pointer` in `normalizeBuiltinSubscriptOperands` only checks
`CanonicalTypeDesc::pointer_levels` and `array_dimensions`. For struct types it returns
`false`, so `0[pw]` is never normalized.

A new helper must:
- Check `desc.category() == TypeCategory::Struct`
- Retrieve the `StructTypeInfo` via `tryGetTypeInfo`
- Scan `member_functions` for conversion operators (`is_conversion_operator() == true`)
- For each, read the actual `FunctionDeclarationNode`'s return-type `TypeSpecifierNode`
  and check `pointer_depth() > 0`
- Return `true` (and optionally the pointee `CanonicalTypeId`) if any such operator exists

**Note:** `StructMemberFunction::conversion_target_type` does **not** capture pointer
levels (it stores only the base `TypeIndex`), so the function declaration's return type
must be inspected directly. `operator int*()` and `operator int()` would both show
`TypeIndex(Int)` in `conversion_target_type`.

The same helper (or a variant) must be added to the inline reversed-detection block in
`inferExpressionType`'s `ArraySubscriptNode` case so that pre-normalization type queries
are also correct.

### 2. Extend `inferExpressionType` element-type logic for struct pointer-conversion

Currently when `array_type_id` refers to a struct, the code falls through to
"Plain type subscript — return base type", which is wrong; the element type should be
the pointee of the conversion operator's return type.

After the normalization check that picks the struct as the array operand, a new branch
is needed:

```
if (array_desc.category() == TypeCategory::Struct) {
    // Find the conversion operator's pointer return type
    // Strip one pointer level to get the element type
}
```

### 3. Extend `tryAnnotateConversion` for struct → pointer

`tryAnnotateConversion` currently exits early at the check:

```cpp
if (!from_desc.pointer_levels.empty() || !to_desc.pointer_levels.empty())
    return false;
```

For the subscript path (and the general `int* p = pw;` case), this must be extended to
allow `Struct → pointer` when a matching conversion operator exists. A new
`StandardConversionKind` may be needed (e.g. `UserDefinedToPointer`) or the existing
`UserDefined` kind can be reused with the target type carrying a non-empty
`pointer_levels`.

`structHasConversionOperatorTo` (the existing sema-level helper) must be extended to
match conversion operators whose function-declaration return type has `pointer_depth > 0`,
since `conversion_target_type` cannot distinguish `operator int()` from `operator int*()`.

### 4. Annotate the subscript array-expr with the pointer conversion

After `normalizeBuiltinSubscriptOperands` confirms the array-side is a struct with a
pointer conversion (and `tryResolveSubscriptOperator` found no `operator[]`), sema must
call `tryAnnotateConversion(array_expr, pointer_type_id)` so that a
`SemanticSlot` with the correct cast kind is written for the array expression. Codegen
can then consume it uniformly without re-doing detection.

### 5. Codegen — `generateArraySubscriptIr`

This is the largest and most risk-prone change. The current function is already complex
with branches for direct arrays, pointer variables, member arrays, multi-dimensional
arrays, pointer-to-array parameters, etc.

A new early branch is needed:

```
After evaluating array_result = visitExpressionNode(array_expr_node):
  if array_result is a struct value AND its sema slot carries a pointer-conversion cast:
      emit a call to operator T*() via emitConversionOperatorCall
      rebind array_result to the returned pointer ExprResult
      update element_type, element_size_bits, element_type_index accordingly
```

`emitConversionOperatorCall` already exists and handles calling a conversion operator
and returning the converted `ExprResult`. The integration point is the section around
line 690 in `IrGenerator_MemberAccess.cpp` just after `array_result` is obtained.

**Red flag:** This change interacts with all existing subscript variants. Any regression
here would be broad. It should be gated behind the sema annotation check so that no
existing code path changes behaviour without a new annotation being present.

---

## Suggested test cases for the new PR

```cpp
// Minimum: forward and reversed, single type
struct IntPtr { int data[4]; operator int*() { return data; } };
// pw[0], 0[pw], pw[1], 1[pw] all correct

// Multiple element types
struct ShortPtr { short data[4]; operator short*() { return data; } };

// Const conversion operator
struct CIntPtr { int data[4]; operator const int*() const { return data; } };
const CIntPtr cp; int x = 0[cp];

// Through a pointer to struct
IntPtr* ppw = ...; int y = 0[*ppw];

// In larger expressions: 0[pw] + 1[pw] == expected sum
```

---

## What is intentionally NOT in scope for that PR

- Conversion operators returning array references (`operator int(&)[N]()`) — these require
  a separate reference-binding path.
- Multiple competing pointer conversion operators (ambiguity error must be diagnosed).
- `operator[]` taking non-integral index types (those are already handled by the
  overloaded-subscript resolution path).
