# FlashCpp Non-Standard Behavior — Code Generation

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/IrGenerator_MemberAccess.cpp`, `src/IrGenerator_Expr_*.cpp`,
`src/IrGenerator_Visitors_Decl.cpp`, `src/IrGenerator_NewDeleteCast.cpp`,
`src/IrGenerator_Call_Direct.cpp`, `src/IrGenerator_Stmt_Decl.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 5.2 No Distinct Pointer Type in the IR `Type` Enum ⚠️

**Standard / ABI:** Pointer arguments must be distinguishable from integer arguments at the
IR level for correct ABI lowering, pointer-arithmetic semantics, and type-based dispatch.

**FlashCpp:** Arrays passed as arguments encode the pointer as a 64-bit value with the
*element* type (e.g., `Char` for `char[]`) rather than a distinct pointer type:

```
// TODO: Add proper pointer type support to the Type enum
```

**Location:** `src/IrGenerator_Call_Direct.cpp:1462`

---

### 5.3 `pointer_depth` Unconditionally Set to 0 in Multiple Places ⚠️

**Standard:** The pointer depth (indirection nesting level) must be tracked accurately for
correct codegen of multi-level pointer operations.

**FlashCpp:** `addr_op.operand.pointer_depth = PointerDepth{};` with the comment `// TODO: Verify pointer
depth` appears at multiple call sites. Multi-level pointer operations (`int**`, `char***`, etc.)
may produce incorrect IR.

**Location:** `src/IrGenerator_Expr_Operators.cpp:412, 1437, 1901`,
`src/IrGenerator_NewDeleteCast.cpp:654`

---

### 8.1 Synthesised Comparison Operators Return `false` When `operator<=>` Not Found ❌

**Standard (C++20 [class.spaceship]):** Synthesised relational operators must only be
generated when `operator<=>` is defaulted. If the spaceship operator is absent the synthesis
must not silently produce an incorrect result.

**FlashCpp:** `IrGenerator_Visitors_Decl.cpp:933` has:

```cpp
// Fallback: operator<=> not found, return false for synthesized comparison operators
emitReturn(IrValue{0ULL}, TypeCategory::Bool, 8, …);
```

If `operator<=>` is not found for any reason, all synthesised comparison operators silently
return `false`. A program using `<` on such a type compiles without error but produces
universally wrong results.

**Location:** `src/IrGenerator_Visitors_Decl.cpp:933`

---

### 8.2 Unsupported `new[]` Element Initialisers Silently Skip Elements ❌

**Standard:** All initialiser forms for heap-allocated arrays must be evaluated.

**FlashCpp:** `IrGenerator_NewDeleteCast.cpp:86–90` and `208–212`:

```cpp
// Skip if the initializer is not supported
if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
    FLASH_LOG(Codegen, Warning, "Unsupported array initializer type, skipping element ", i);
    continue;
}
```

An unsupported initialiser type (e.g., certain nested designated initialisers) causes the
array element to be left uninitialised with only a warning log.

**Location:** `src/IrGenerator_NewDeleteCast.cpp:86–90, 208–212`

---

### 8.3 Unimplemented Unary Operators Throw `InternalError` Instead of a User Diagnostic ⚠️

**Standard:** All unary operators on supported types must compile; unsupported combinations
must produce a compile error with a meaningful message.

**FlashCpp:** `IrGenerator_Expr_Conversions.cpp:1592` throws `InternalError("Unary operator not
implemented yet")` in the catch-all else branch, producing a compiler crash rather than a
user-visible error.

**Location:** `src/IrGenerator_Expr_Conversions.cpp:1592`

---

### 8.4 Parser Can Return `size_bits = 0` for Valid Variable Declarations ⚠️

**Standard:** All declared variables must have a known, non-zero type size.

**FlashCpp:** `IrGenerator_Expr_Primitives.cpp:256–262` still contains an explicit workaround:

```cpp
// Fallback: if size_bits is 0, calculate from type (parser bug workaround)
FLASH_LOG(Codegen, Warning,
    "Parser returned size_bits=0 for identifier '...' (type=...) - using fallback calculation");
size_bits = getTypeSpecSizeBits(type_node);
```

The fallback is better than the earlier scalar-only version because it now delegates to
`getTypeSpecSizeBits(type_node)`, but the underlying parser bug still exists and codegen still
needs to recover from it late.

**Location:** `src/IrGenerator_Expr_Primitives.cpp:254–262`
