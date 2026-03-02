# TODO / FIXME Analysis

**Date**: 2026-03-01  
**Total items found**: 47 (43 TODO + 4 FIXME/minor)  
**Search scope**: `src/**/*.cpp`, `src/**/*.h`

Each item is assessed as one of:
- ✅ **Valid** – gap that genuinely exists in the compiler today
- ⚠️ **Stale** – the underlying feature is already implemented; the comment is misleading
- 🔍 **Needs investigation** – unclear whether the gap still exists

---

## 1. Codegen – Function Pointer Call Return Types

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Call_Indirect.cpp` | 399 | ✅ Valid |
| `src/CodeGen_Call_Indirect.cpp` | 539 | ✅ Valid |
| `src/CodeGen_Call_Indirect.cpp` | 603 | ✅ Valid |

When a struct data member has type `FunctionPointer` and is called through a member-function-call expression, the return type of the generated `IndirectCallOp` is hardcoded:

- Line 401: `return { Type::Void, 0, ret_var, 0ULL };` — always Void  
- Line 605: `return { Type::Int, 32, ret_var, 0ULL };` — always Int

The `TypeSpecifierNode` stored alongside the `FunctionPointer` member contains the real return type. C++20 requires that the return type of an indirect call matches the called function's declared type; both hardcoded fallbacks will produce wrong code for any function pointer member that returns something other than `void`/`int`.

---

## 2. Template Expression Substitutor

| File | Line | Status |
|------|------|--------|
| `src/ExpressionSubstitutor.cpp` | 1069 | ✅ Valid |
| `src/ExpressionSubstitutor.cpp` | 1123 | ✅ Valid |

**Line 1069** – `substituteType()` can build a new base-class template instantiation only for single-argument templates (`base_trait<T>`). Multi-argument bases such as `std::pair<T, U>` or `integral_constant<bool, N>` will have their substitution silently skipped. The fix requires splitting `args_str` on commas and substituting each argument independently.

**Line 1123** – `ensureTemplateInstantiated()` is a stub with an empty body. It is currently called from the substitutor when a base class name is encountered, but no instantiation is actually triggered. This is lower priority because the caller often falls back gracefully, but it means recursive template chains may not be fully resolved.

---

## 3. Preprocessor – `#line` Directive Filename

| File | Line | Status |
|------|------|--------|
| `src/FileReader_Macros.cpp` | 1291 | ✅ Valid |

`#line N "filename"` is required by the C++ standard (§15.7 [cpp.line]) to update both the current line number and the presumed source-file name used in diagnostics. The line-number update is implemented; the filename update is skipped entirely with the comment "to avoid lifetime issues". The fix requires interning the filename string into the `StringTable` (already available) and storing the resulting `StringHandle` in the file-stack entry.

---

## 4. IR Converter – Error Handling and SSE Moves

| File | Line | Status |
|------|------|--------|
| `src/IRConverter_Conv_CorePrivate.h` | 504 | ✅ Fixed |
| `src/IRConverter_Conv_CorePrivate.h` | 726 | ✅ Fixed |
| `src/IRConverter_Conv_CorePrivate.h` | 1067 | ✅ Fixed |

**Lines 504 / 726** – ~~Both `throw InternalError("Missing variable name")` sites carry `// TODO: Error handling`. The real improvement would be to include the variable name (or the surrounding instruction) in the error message to help diagnose which variable is missing.~~ **Fixed**: Error messages now include the variable name for easier diagnosis.

**Line 1067** – ~~Float register-to-register moves in the IR converter currently throw `InternalError` rather than emitting an `MOVSS`/`MOVSD`.~~ **Fixed**: Now uses the existing `emitFloatMovRegToReg()` helper to emit proper `MOVSS`/`MOVSD` register-to-register moves based on whether the type is float or double.

---

## 5. Template Class – Member Struct Base Class Parsing

| File | Line | Status |
|------|------|--------|
| `src/Parser_Templates_Class.cpp` | 4640 | ✅ Valid |
| `src/Parser_Templates_Class.cpp` | 5007 | ✅ Valid |

Both sites handle member `struct` templates (both partial specializations and primary templates) that have base classes. Currently all tokens between `:` and `{` are consumed and discarded:

```cpp
while (peek() != "{"_tok) { advance(); }   // base class info lost
```

This means the compiler silently ignores base classes of member struct templates, so member functions and data members inherited from those bases are unavailable. The existing helpers `consume_base_class_qualifiers_after_template_args()` and `build_template_arg_infos()` (in `Parser_Expr_QualLookup.cpp`) should be reused here as they are already used in three other call sites.

---

## 6. Declarator Parsing

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_DeclaratorCore.cpp` | 898 | ✅ Valid |
| `src/Parser_Decl_DeclaratorCore.cpp` | 993 | ✅ Fixed |

**Line 898** – `parse_direct_declarator()` handles only the simple identifier form. The C++ grammar (§9.3 [dcl.decl]) also allows *parenthesized* direct-declarators: `(*fp)(params)` for function pointers, `(&r)` for reference declarators, and `(a[N])` for arrays. Without this, certain function-pointer variable declarations fail to parse.

**Line 993** – ~~The `linkage` parameter of `parse_direct_declarator()` is annotated `[[maybe_unused]]` and then immediately overwritten with `Linkage::None` in the generated `FunctionSignature`. This loses `extern "C"` linkage on function pointer type declarations.~~ **Fixed**: The `linkage` parameter is now threaded through `parse_direct_declarator()` → `parse_postfix_declarator()` → `FunctionSignature.linkage`, preserving `extern "C"` linkage.

---

## 7. `parse_struct_declaration()` – Missing Specifier Propagation

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_FunctionOrVar.cpp` | 28 | ✅ Valid |

When a declaration like `inline constexpr struct Foo { ... } var = {};` is parsed, the `is_constexpr` / `is_inline` flags are already consumed before `parse_struct_declaration()` is called, but they are never passed to it. The trailing variable declaration `var` therefore loses its `constexpr` qualification. This affects constant-expression initializer validation.

---

## 8. Constexpr Evaluation Gaps

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_FunctionOrVar.cpp` | 1005 | ✅ Valid |
| `src/ConstExprEvaluator_Members.cpp` | 202 | ✅ Valid |
| `src/ConstExprEvaluator_Members.cpp` | 1473 | ✅ Valid |
| `src/ConstExprEvaluator_Core.cpp` | 1050 | ✅ Valid |

- **FunctionOrVar.cpp:1005** – `constexpr` variables whose initializers are `InitializerListNode`s, casts, or other complex expressions bypass evaluation entirely. A full implementation would recursively evaluate those forms.
- **Members.cpp:202** – Inside a constexpr member function, accesses of the form `this->x` (stored as `MemberAccessNode`) fall through to the non-mutable evaluator, which cannot modify the `bindings` map. This blocks constexpr member functions that write to `*this`.
- **Members.cpp:1473** – Array subscript followed by member access (`arr[0].member`) in a constexpr context returns an error. This pattern is common in standard-library constexpr implementations.
- **Core.cpp:1050** – Calling `operator()` on a user-defined functor in a constexpr context is not implemented, returning an error immediately. This blocks `std::less`, `std::greater`, and any user comparator in constexpr.

---

## 9. Overload Resolution

| File | Line | Status |
|------|------|--------|
| `src/OverloadResolution.h` | 684 | ✅ Valid |
| `src/OverloadResolution.h` | 700 | ✅ Valid |
| `src/SymbolTable.h` | 484 | ✅ Valid |

- **OverloadResolution.h:684** – `findBinaryOperatorOverload()` returns the *first* member `operator` with a matching symbol, without verifying the parameter types. When a class defines both `operator+(const Foo&)` and `operator+(int)`, the wrong overload may be selected.
- **OverloadResolution.h:700** – Free-function operator overloads (e.g., `operator+(A, B)` defined at namespace scope) are never searched. C++20 §12.4 [over.match.oper] requires that both member and non-member candidates be gathered before overload resolution.
- **SymbolTable.h:484** – `get_overload()` always returns `overloads[0]` when multiple overloads exist, skipping exact-match and implicit-conversion ranking entirely. This affects every multi-overload function call.

---

## 10. Missing Return Statement Diagnostic

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Visitors_Decl.cpp` | 860 | ✅ Valid |

A non-void function whose body has no `return` statement on every code path is silently accepted. The comment correctly notes this should be a `CompileError`. Full enforcement requires a simple control-flow reachability pass over the function's basic blocks, similar to the existing flow used for `main`'s implicit return-0 injection.

---

## 11. Template Argument Deduction – Non-Type Parameters

| File | Line | Status |
|------|------|--------|
| `src/Parser_Templates_Inst_Deduction.cpp` | 698 | ✅ Valid |

`try_deduce_template_arguments_from_call()` only deduces *type* parameters from function arguments. Non-type parameters (e.g., `template<int N>` deduced from `std::array<T,3>`) and pointer/reference patterns are not handled. This limits deduction to simple `template<typename T> void f(T)` cases.

---

## 12. Template Instantiation Phase Labels (Stale)

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Visitors_TypeInit.cpp` | 133 | ✅ Already fixed |
| `src/CodeGen_Visitors_TypeInit.cpp` | 138 | ✅ Already fixed |

Both comments have already been updated to correctly explain *why* the early return is correct:

```cpp
// Template declarations produce no IR of their own; IR is generated when each
// instantiation is visited (see try_instantiate_class_template / try_instantiate_function_template).
```

No further action needed.

---

## 13. Complex Pack Expansion Rewriting

| File | Line | Status |
|------|------|--------|
| `src/Parser_Expr_PrimaryExpr.cpp` | 3306 | ✅ Valid |

When a pack-expansion argument contains a non-trivial expression (e.g., `f(g(args)...)` where `g` wraps each element), the parser logs an error and falls back to copying the unexpanded node. Only the simple `identifier...` pattern is rewritten. Full C++20 fold-expression and pack-expansion rewriting (§13.7.4 [temp.variadic]) requires a recursive AST rewriter that can substitute every occurrence of a pack within an arbitrary expression.

---

## 14. Placement `new` – Multiple Arguments (FIXME)

| File | Line | Status |
|------|------|--------|
| `src/Parser_Expr_PrimaryUnary.cpp` | 361 | ✅ Valid |

`new (a, b) T` is valid C++ (§7.6.2.8 [expr.new]) and is used in some allocator implementations. `NewExpressionNode` currently stores only a single `placement_address`; when multiple placement arguments are parsed, only the first is stored and the rest are silently dropped. The fix requires adding a `std::vector<ASTNode> placement_args` field to `NewExpressionNode` and updating the IR generator to pass all arguments to the placement `operator new`.

---

## 15. Lambda-to-Function-Pointer Conversion Type Node

| File | Line | Status |
|------|------|--------|
| `src/Parser_Expr_PrimaryUnary.cpp` | 998 | ✅ Valid |

When a captureless lambda is cast with `+lambda` or an explicit `static_cast` to a function pointer, the parser currently returns the lambda node unchanged and relies on the code generator to handle the conversion. No `TypeSpecifierNode` with `Type::FunctionPointer` is created. This means type-checking of the resulting expression is incomplete; the type of the cast expression should be the function pointer type matching the lambda's `operator()`.

---

## 16. Copy Constructor Detection – Type Index Check

| File | Line | Status |
|------|------|--------|
| `src/AstNodeTypes.cpp` | 647 | ✅ Fixed |

~~`StructTypeInfo::findCopyConstructor()` identifies a copy constructor by checking that a single parameter is a `const Struct&`, but does not verify that `param_type.type_index()` equals the enclosing struct's own `type_index_`.~~ **Fixed**: Both `findCopyConstructor()` and `findMoveConstructor()` now look up the struct's own `type_index_` via `gTypesByName` and verify it matches `param_type.type_index()`. A constructor like `Foo(const Bar&)` is no longer misidentified as a copy constructor for `Foo`.

---

## 17. `pointer_depth` in Address-Of Operations (Minor Cleanup)

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Expr_Operators.cpp` | 585, 662 | 🔍 Needs investigation |
| `src/CodeGen_Visitors_Namespace.cpp` | 334 | 🔍 Needs investigation |
| `src/CodeGen_Stmt_Decl.cpp` | 808, 1154, 1564 | 🔍 Needs investigation |
| `src/CodeGen_NewDeleteCast.cpp` | 673 | 🔍 Needs investigation |
| `src/CodeGen_Lambdas.cpp` | 1841 | 🔍 Needs investigation |

All seven sites set `addr_op.operand.pointer_depth = 0` when generating an `AddressOf` IR operand. For simple variables this is correct, but for expressions that are already pointers (e.g., `&(*ptr)` or `&ptr->member`) the depth should be incremented from the operand's depth. The current code may produce incorrect pointer arithmetic for multi-level indirection. These sites need a dedicated test with pointer-to-pointer variables before fixing.

---

## 18. Template Template Parameter Default Arguments

| File | Line | Status |
|------|------|--------|
| `src/Parser_Templates_Params.cpp` | 119 | ✅ Valid |

`parse_template_parameter()` does not handle default arguments for template-template parameters: `template<template<typename> class Container = std::vector>`. After the parameter name is parsed, a `=` would start the default argument, but the parser immediately returns. The token stream following the `=` will then cause a parse error. Default template template arguments are required by several standard-library traits.

---

## 19. Concept Template Arguments Skipped

| File | Line | Status |
|------|------|--------|
| `src/Parser_Templates_Params.cpp` | 162 | ✅ Valid |

When a constrained type parameter has template arguments on the concept — `Concept<U> T` — the `<U>` part is consumed by skipping balanced angle brackets without storing `U`. The stored `TemplateParameterNode` therefore has no knowledge of the concept's argument binding. This affects partial concept specializations and `requires` clauses that use parameterized concepts.

---

## 20. Array Member Length in Code Generation

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_MemberAccess.cpp` | 447 | ✅ Valid |

When generating a subscript into an array data member, the total size of the member is known but the element count is reconstructed heuristically:

```cpp
if (base_element_size > 0 && element_size_bits > base_element_size)
    element_size_bits = base_element_size;
```

The actual array length is not stored in `StructMemberInfo`. Without it the bounds of the member-array are unknown, preventing proper bounds-check generation and correct multi-dimensional array layout.

---

## 21. Type Traits – Incomplete Checks

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_MemberAccess.cpp` | 2360 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2372 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2380 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2557 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2635 | ✅ Valid |

Five type-trait intrinsics use heuristics instead of the correct C++ standard definitions:

| Trait | Current heuristic | What's missing |
|-------|------------------|----------------|
| `__is_trivially_copyable` | `!has_vtable` | Check for trivial copy/move ctors and trivial dtor |
| `__is_trivially_copyable` (struct path) | `!has_vtable` | Same as above |
| `__is_trivial` | `!has_vtable && !has_user_ctor` | Also need trivial copy/move/dtor |
| `__is_nothrow_constructible` | `!has_vtable && !has_user_ctor` | Inspect `noexcept` on each ctor |
| `__is_nothrow_assignable` | `!has_vtable` | Inspect `noexcept` on assignment ops |

These traits are queried extensively by `<type_traits>` and determine which standard-library optimizations are enabled. Incorrect results can silently produce wrong codegen for containers that rely on these traits to select between memcpy and element-wise copy.

---

## 22. Pointer Type in `Type` Enum

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Call_Direct.cpp` | 932 | ✅ Valid |

When an array argument is passed to a function, the compiler emits `AddressOf` and then uses the *element* type with a hardcoded 64-bit size to represent the pointer:

```cpp
irOperands.emplace_back(type_node.type());  // Element type, not Pointer type
irOperands.emplace_back(64);               // Pointer size
```

Adding a `Type::Pointer` enumerator (or a dedicated `pointer_depth` field to `IrOperand`) would allow the IR to precisely represent the pointer's type and enable proper pointer arithmetic and type checking through the IR pipeline.

---

## 23. `main` Special-Case in Line Mapping

| File | Line | Status |
|------|------|--------|
| `src/IRConverter_Conv_VarDecl.h` | 1733 | 🔍 Needs investigation |

The `Return` handler skips `addLineMapping()` for `main`:

```cpp
if (instruction.getLineNumber() > 0 &&
    current_function_name_ != StringTable::getOrInternStringHandle("main"))
```

The comment asks whether this special case is still necessary. The original reason was that `main`'s implicit return-0 was being double-mapped. Since line-mapping logic was refactored, it should be verified whether removing this guard regresses any debugger-step behaviour. Until verified, the guard should stay.

---

## Summary Table

| Category | Count | Status |
|----------|-------|--------|
| Function pointer return types (indirect call) | 3 | ✅ Valid |
| Template substitutor gaps | 2 | ✅ Valid |
| Preprocessor `#line` filename | 1 | ✅ Valid |
| IR converter error messages / SSE moves | 3 | ✅ Fixed |
| Member struct template base classes | 2 | ✅ Valid |
| Declarator parsing gaps | 2 | 1 ✅ Valid, 1 ✅ Fixed |
| Specifier propagation to struct decl | 1 | ✅ Valid |
| Constexpr evaluation gaps | 4 | ✅ Valid |
| Overload resolution | 3 | ✅ Valid |
| Missing return diagnostic | 1 | ✅ Valid |
| Template deduction non-type params | 1 | ✅ Valid |
| Phase labels (stale) | 2 | ✅ Already fixed |
| Complex pack expansion | 1 | ✅ Valid |
| Placement new multiple args | 1 | ✅ Valid |
| Lambda-to-function-pointer type | 1 | ✅ Valid |
| Copy constructor type_index check | 1 | ✅ Fixed |
| `pointer_depth` in address-of | 7 | 🔍 Needs investigation |
| Template template parameter defaults | 1 | ✅ Valid |
| Concept template arguments | 1 | ✅ Valid |
| Array member length | 1 | ✅ Valid |
| Type traits incomplete checks | 5 | ✅ Valid |
| `Type::Pointer` enum gap | 1 | ✅ Valid |
| `main` line-mapping guard | 1 | 🔍 Needs investigation |
| **Total** | **47** | |

**Stale**: 0 items (Phase-label comments already updated)  
**Fixed**: 7 items (IR error messages ×2, SSE moves ×1, linkage forwarding ×1, copy/move ctor type_index ×1, stale comments ×2)  
**Needs investigation before fixing**: 8 items (pointer_depth sites + `main` guard)  
**Genuinely unimplemented**: 32 items
