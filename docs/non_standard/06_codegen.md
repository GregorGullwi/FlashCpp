# FlashCpp Non-Standard Behavior — Code Generation

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/CodeGen_MemberAccess.cpp`, `src/CodeGen_Expr_*.cpp`,
`src/CodeGen_Visitors_Decl.cpp`, `src/CodeGen_NewDeleteCast.cpp`,
`src/CodeGen_Call_Direct.cpp`, `src/CodeGen_Stmt_Decl.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.4 Conversion Operator Lookup Uses "operator user_defined" Workaround ⚠️

**Standard (C++20 [class.conv.fct]):** Conversion functions are named `operator T()`.
Lookup proceeds through normal member name lookup.

**FlashCpp:** For conversion operators whose return type was stored as `UserDefined` (often
via a template type alias), the compiler falls back to searching for the string literal
`"operator user_defined"` and uses type-size matching as a tiebreaker
(CodeGen_MemberAccess.cpp:3469–3525). This can produce "undefined reference to
`operator user_defined`" linker errors for pattern templates.

**Location:** `src/CodeGen_MemberAccess.cpp:3469–3525`

---

### 5.2 No Distinct Pointer Type in the IR `Type` Enum ⚠️

**Standard / ABI:** Pointer arguments must be distinguishable from integer arguments at the
IR level for correct ABI lowering, pointer-arithmetic semantics, and type-based dispatch.

**FlashCpp:** Arrays passed as arguments encode the pointer as a 64-bit value with the
*element* type (e.g., `Char` for `char[]`) rather than a distinct pointer type:

```
// TODO: Add proper pointer type support to the Type enum
```

**Location:** `src/CodeGen_Call_Direct.cpp:978`

---

### 5.3 `pointer_depth` Unconditionally Set to 0 in Multiple Places ⚠️

**Standard:** The pointer depth (indirection nesting level) must be tracked accurately for
correct codegen of multi-level pointer operations.

**FlashCpp:** `addr_op.operand.pointer_depth = 0;` with the comment `// TODO: Verify pointer
depth` appears at ≥ 7 call sites. Multi-level pointer operations (`int**`, `char***`, etc.)
may produce incorrect IR.

**Location:** `src/CodeGen_Expr_Operators.cpp:863, 940`,
`src/CodeGen_NewDeleteCast.cpp:672`,
`src/CodeGen_Visitors_Decl.cpp:2460`,
`src/CodeGen_Visitors_Namespace.cpp:338`,
`src/CodeGen_Stmt_Decl.cpp:886, 1239, 1651`

---

### 8.1 Synthesised Comparison Operators Return `false` When `operator<=>` Not Found ❌

**Standard (C++20 [class.spaceship]):** Synthesised relational operators must only be
generated when `operator<=>` is defaulted. If the spaceship operator is absent the synthesis
must not silently produce an incorrect result.

**FlashCpp:** `CodeGen_Visitors_Decl.cpp:705–706` has:

```cpp
// Fallback: operator<=> not found, return false for all synthesized operators
emitReturn(IrValue{0ULL}, Type::Bool, 8, …);
```

If `operator<=>` is not found for any reason, all synthesised comparison operators silently
return `false`. A program using `<` on such a type compiles without error but produces
universally wrong results.

**Location:** `src/CodeGen_Visitors_Decl.cpp:705–706`

---

### 8.2 Unsupported `new[]` Element Initialisers Silently Skip Elements ❌

**Standard:** All initialiser forms for heap-allocated arrays must be evaluated.

**FlashCpp:** `CodeGen_NewDeleteCast.cpp:94–97` and `224–230`:

```cpp
// Skip if the initializer is not supported
if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
    FLASH_LOG(Codegen, Warning, "Unsupported array initializer type, skipping element ", i);
    continue;
}
```

An unsupported initialiser type (e.g., certain nested designated initialisers) causes the
array element to be left uninitialised with only a warning log.

**Location:** `src/CodeGen_NewDeleteCast.cpp:94–97, 224–230`

---

### 8.3 Unimplemented Unary Operators Throw `InternalError` Instead of a User Diagnostic ⚠️

**Standard:** All unary operators on supported types must compile; unsupported combinations
must produce a compile error with a meaningful message.

**FlashCpp:** `CodeGen_Expr_Conversions.cpp:1514` throws `InternalError("Unary operator not
implemented yet")` in the catch-all else branch, producing a compiler crash rather than a
user-visible error.

**Location:** `src/CodeGen_Expr_Conversions.cpp:1514`

---

### 8.4 Parser Can Return `size_bits = 0` for Valid Variable Declarations ⚠️

**Standard:** All declared variables must have a known, non-zero type size.

**FlashCpp:** `CodeGen_Expr_Primitives.cpp:228–232` contains an explicit workaround:

```cpp
// Fallback: if size_bits is 0, calculate from type (parser bug workaround)
FLASH_LOG(Codegen, Warning,
    "Parser returned size_bits=0 for identifier '...' (type=...) - using fallback calculation");
size_bits = get_type_size_bits(type_node.type());
```

The fallback uses only the scalar type kind and ignores struct sizes entirely; struct
variables that hit this path may be given the wrong size.

**Location:** `src/CodeGen_Expr_Primitives.cpp:228–232`
