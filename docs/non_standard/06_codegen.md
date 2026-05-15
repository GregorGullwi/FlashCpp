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


---

### 8.5 Member `get` Template Specializations Were Never Code-Generated ✅ Fixed

**Standard ([dcl.struct.bind]/3):** When a type satisfies the tuple-like protocol, structured
bindings must call `std::get<I>(e)` or, if found by name lookup, a member function template
`e.get<I>()`.  The compiler must emit a definition for every explicit specialization it calls.

**FlashCpp (before fix):** `Parser_Templates_MemberOutOfLine.cpp` handled explicit member
function template specializations (e.g., `template <> int Point::get<0>() const { … }`) by:

1. Skipping the body with `skip_balanced_braces()`.
2. Saving the body position for *lazy* instantiation (`set_template_body_position`).
3. Registering the specialization with the template registry.
4. **Never** adding the function node to the AST.

Because the node was absent from the top-level AST, the IrGenerator never visited it and no
object-code definition was emitted, producing a linker error:

```
undefined reference to `_ZN5Point3getILm0EEEv'
```

A second bug compounded this: the `const` qualifier captured in `member_quals` was never
forwarded to `func_ref.set_is_const_member_function(…)`, so the call site (in
`SemanticAnalysis.cpp`) computed the wrong mangled name (missing `K`).

**Fix (`src/Parser_Templates_MemberOutOfLine.cpp`, `is_specialization` branch):**

* `func_ref.set_is_const_member_function(member_quals.is_const())` — propagate `const`.
* Non-type template args (e.g., `0` from `<0>`) are extracted and stored on the node via
  `set_non_type_template_args`, then the correct ABI mangled name is computed using
  `NameMangling::generateMangledNameWithTemplateArgs` (or `…WithTypeTemplateArgs` for type
  args) with the struct name and `is_const_method` flag.
* When there are no outer template parameters (`template_params.empty()`) the body is parsed
  **immediately**: the lexer is rewound to `body_start`, `parse_function_body()` is called
  inside a `FunctionParsingScopeGuard` (for correct member context and `this` injection), the
  definition is set on the node, and the node is appended to the AST via `appendUserNode` and
  to the symbol table via `gSymbolTable.insert`.
* When outer template parameters are present (template class member), the original lazy-body
  path is preserved (`set_template_body_position`).

**Locations:**
- Fix: `src/Parser_Templates_MemberOutOfLine.cpp:758–870`
- Call site: `src/SemanticAnalysis.cpp` (`normalizeStructuredBinding`, member-get mangling)
- Test: `tests/test_structured_binding_member_get_ret42.cpp`
