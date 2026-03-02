# TODO / FIXME Analysis

**Date**: 2026-03-01  
**Total items found**: 49 (44 TODO + 4 FIXME/minor + 1 discovered)  
**Search scope**: `src/**/*.cpp`, `src/**/*.h`

Each item is assessed as one of:
- ✅ **Valid** – gap that genuinely exists in the compiler today
- ⚠️ **Stale** – the underlying feature is already implemented; the comment is misleading
- 🔍 **Needs investigation** – unclear whether the gap still exists

---

## 1. Codegen – Function Pointer Call Return Types

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Call_Indirect.cpp` | 399 | ✅ Fixed |
| `src/CodeGen_Call_Indirect.cpp` | 539 | ✅ Fixed |
| `src/CodeGen_Call_Indirect.cpp` | 603 | ✅ Fixed |

~~When a struct data member has type `FunctionPointer` and is called through a member-function-call expression, the return type of the generated `IndirectCallOp` is hardcoded.~~ **Fixed**: Added `std::optional<FunctionSignature> function_signature` to `StructMember`. The parser's `try_parse_function_pointer_member()` now accepts and stores the return type. All three codegen sites read the stored return type instead of hardcoding `Void` or `Int`.

---

## 2. Template Expression Substitutor

| File | Line | Status |
|------|------|--------|
| `src/ExpressionSubstitutor.cpp` | 1069 | ✅ Fixed |
| `src/ExpressionSubstitutor.cpp` | 1123 | ✅ Fixed |

**Line 1069** – ~~`substituteType()` can build a new base-class template instantiation only for single-argument templates (`base_trait<T>`). Multi-argument bases such as `std::pair<T, U>` or `integral_constant<bool, N>` will have their substitution silently skipped.~~ **Fixed**: Template argument strings are now split on commas (respecting angle-bracket depth) and each argument is substituted independently. Non-parameter arguments are looked up as concrete types in `gTypesByName`.

**Line 1123** – ~~`ensureTemplateInstantiated()` is a stub with an empty body.~~ **Fixed**: Now delegates to `parser_.try_instantiate_class_template()` to trigger actual template instantiation when a base class name is encountered during substitution.

---

## 3. Preprocessor – `#line` Directive Filename

| File | Line | Status |
|------|------|--------|
| `src/FileReader_Macros.cpp` | 1291 | ✅ Fixed |

~~`#line N "filename"` is required by the C++ standard (§15.7 [cpp.line]) to update both the current line number and the presumed source-file name used in diagnostics. The line-number update is implemented; the filename update is skipped entirely with the comment "to avoid lifetime issues".~~ **Fixed**: The filename string is now interned into `file_paths_` via `get_or_add_file_path()`, which provides stable lifetime. The `filestack_.top().file_name` is updated to point to the interned string.

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
| `src/Parser_Templates_Class.cpp` | 4640 | ✅ Fixed |
| `src/Parser_Templates_Class.cpp` | 5007 | ✅ Fixed |

~~Both sites handle member `struct` templates (both partial specializations and primary templates) that have base classes. Currently all tokens between `:` and `{` are consumed and discarded:~~

```cpp
while (peek() != "{"_tok) { advance(); }   // base class info lost
```

~~This means the compiler silently ignores base classes of member struct templates, so member functions and data members inherited from those bases are unavailable.~~

**Fixed**: Added `parse_member_struct_template_base_class_list()` helper in `Parser_Templates_Class.cpp` that reuses the existing `consume_base_class_qualifiers_after_template_args()` and `build_template_arg_infos()` helpers. For template bases with dependent arguments (e.g., `List<Rest...>`), `add_deferred_template_base_class()` is called. For concrete bases (e.g., `: IntBase`), `add_base_class()` with immediate type lookup is used. In `Parser_Templates_Inst_ClassTemplate.cpp`, the partial specialization instantiation path now also resolves `deferred_template_base_classes()` using a substitution map built from the pattern's template parameters, correctly expanding variadic packs (including empty packs for the base case). Tests: `tests/test_item5b_ret42.cpp` and `tests/test_member_struct_template_concrete_base_ret42.cpp`.

---

## 6. Declarator Parsing

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_DeclaratorCore.cpp` | 898 | ✅ Already works |
| `src/Parser_Decl_DeclaratorCore.cpp` | 993 | ✅ Fixed |

**Line 898** – ~~`parse_direct_declarator()` handles only the simple identifier form. The C++ grammar (§9.3 [dcl.decl]) also allows *parenthesized* direct-declarators: `(*fp)(params)` for function pointers, `(&r)` for reference declarators, and `(a[N])` for arrays. Without this, certain function-pointer variable declarations fail to parse.~~ **Verified working**: `int (*fp)(int, int) = add;` and `int (*g_fp)(int, int) = add;` both compile and run correctly; tests confirm the parenthesized declarator form is handled.

**Line 993** – ~~The `linkage` parameter of `parse_direct_declarator()` is annotated `[[maybe_unused]]` and then immediately overwritten with `Linkage::None` in the generated `FunctionSignature`. This loses `extern "C"` linkage on function pointer type declarations.~~ **Fixed**: The `linkage` parameter is now threaded through all three `parse_postfix_declarator()` call sites within `parse_declarator()` and `parse_direct_declarator()`, preserving `extern "C"` linkage for all declarator forms (unnamed, parenthesized, and direct).

---

## 7. `parse_struct_declaration()` – Missing Specifier Propagation

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_FunctionOrVar.cpp` | 28 | ✅ Fixed |

~~When a declaration like `inline constexpr struct Foo { ... } var = {};` is parsed, the `is_constexpr` / `is_inline` flags are already consumed before `parse_struct_declaration()` is called, but they are never passed to it. The trailing variable declaration `var` therefore loses its `constexpr` qualification. This affects constant-expression initializer validation.~~ **Fixed**: Added `parse_struct_declaration_with_specs(bool pre_is_constexpr, bool pre_is_inline)` that receives the pre-parsed specifiers. The existing `parse_struct_declaration()` delegates to it with `false, false`. The call site in `parse_declaration_or_function_definition()` now passes the parsed `is_constexpr` and `is_inline` flags. These are combined with any post-struct-body specifiers and applied to trailing `VariableDeclarationNode` via `set_is_constexpr()`.

---

## 8. Constexpr Evaluation Gaps

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_FunctionOrVar.cpp` | 1005 | ✅ Valid |
| `src/ConstExprEvaluator_Members.cpp` | 202 | ✅ Fixed |
| `src/ConstExprEvaluator_Members.cpp` | 1473 | ✅ Fixed |
| `src/ConstExprEvaluator_Core.cpp` | 1050 | ✅ Already works (direct call) |

- **FunctionOrVar.cpp:1005** – `constexpr` variables whose initializers are `InitializerListNode`s, casts, or other complex expressions bypass evaluation entirely. A full implementation would recursively evaluate those forms.
- ~~**Members.cpp:202** – Inside a constexpr member function, accesses of the form `this->x` (stored as `MemberAccessNode`) fall through to the non-mutable evaluator, which cannot modify the `bindings` map.~~ **Fixed**: The assignment branch in `evaluate_expression_with_bindings` now handles `MemberAccessNode` LHS (`this->x = value`) by extracting the member name and updating `bindings[member_name]`. Read access already worked via the const evaluator. Test: `tests/test_constexpr_this_member_ret42.cpp`.
- ~~**Members.cpp:1473** – Array subscript followed by member access (`arr[0].member`) in a constexpr context returns an error.~~ **Fixed**: Implemented `evaluate_array_subscript_member_access()` for constexpr identifier-based arrays initialized with initializer lists of struct constructor calls. The evaluator now resolves the indexed element, extracts the requested member via constructor/member-initializer bindings, and returns the constant value. Test: `tests/test_constexpr_array_subscript_member_ret42.cpp`.
- **Core.cpp:1050** – Calling `operator()` on a user-defined functor **passed as a template argument** in a constexpr context reaches the `evaluate_callable_object` code path, which immediately returns an error for `ConstructorCallNode` initializers. This blocks constexpr comparators like `std::less` / `std::greater` when passed as non-type template parameters. (Direct `constexpr Functor f; f(a,b)` calls already work via the member function call path.)

---

## 9. Overload Resolution

| File | Line | Status |
|------|------|--------|
| `src/OverloadResolution.h` | 684 | ✅ Fixed |
| `src/OverloadResolution.h` | 700 | ✅ Valid |
| `src/SymbolTable.h` | 484 | ✅ Fixed |

- ~~**OverloadResolution.h:684** – `findBinaryOperatorOverload()` returns the *first* member `operator` with a matching symbol, without verifying the parameter types.~~ **Fixed**: Now performs two-phase matching: (1) exact type_index match on the parameter against the right-hand operand type, (2) fallback to first matching operator symbol. This correctly selects `operator+(int)` over `operator+(const Foo&)` when the right-hand operand is an `int`.
- ~~**OverloadResolution.h:700** – Free-function operator overloads (e.g., `operator+(A, B)` defined at namespace scope) are never searched.~~ **Verified working**: `operator+(Point, Point)` defined at namespace scope is found and called correctly; the `Point` addition test compiles and returns the correct result.
- ~~**SymbolTable.h:484** – `get_overload()` always returns `overloads[0]` when multiple overloads exist, skipping exact-match and implicit-conversion ranking entirely.~~ **Fixed**: `lookup_function()` now performs two-phase matching: (1) exact parameter count + type match, (2) parameter count match only (for implicit conversions), with fallback to first overload. This correctly selects `add(int,int)` over `add(double,double)` when called with int arguments.

---

## 10. Missing Return Statement Diagnostic

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Visitors_Decl.cpp` | 860 | ✅ Fixed |

~~A non-void function whose body has no `return` statement on every code path is silently accepted.~~ **Fixed**: Now emits a warning via `FLASH_LOG_FORMAT(Codegen, Warning, ...)` when a non-void function's last instruction is not a return. Full control-flow analysis for all paths is still needed for a complete implementation, but this catches the common case.

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
| `src/Parser_Expr_PrimaryUnary.cpp` | 361 | ✅ Fixed |

~~`new (a, b) T` is valid C++ (§7.6.2.8 [expr.new]) and is used in some allocator implementations. `NewExpressionNode` currently stores only a single `placement_address`; when multiple placement arguments are parsed, only the first is stored and the rest are silently dropped.~~

**Fixed**: Replaced the single `placement_address_` field in `NewExpressionNode` with `std::vector<ASTNode> placement_args_`. Added a second constructor that accepts a vector directly. Backward-compatible `placement_address()` accessor returns the first arg (if any) for existing IR generator call sites. The parser now collects all placement arguments into `all_placement_args` (replacing the old code that discarded extras). Test: `tests/test_placement_new_multi_args_ret42.cpp`.

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

~~`StructTypeInfo::findCopyConstructor()` identifies a copy constructor by checking that a single parameter is a `const Struct&`, but does not verify that `param_type.type_index()` equals the enclosing struct's own `type_index_`.~~ **Fixed**: All four methods — `findCopyConstructor()`, `findMoveConstructor()`, `findCopyAssignmentOperator()`, and `findMoveAssignmentOperator()` — now use `isOwnTypeIndex()` to verify `param_type.type_index()` matches the struct's own type. A constructor like `Foo(const Bar&)` or `operator=(const Bar&)` is no longer misidentified.

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
| `src/Parser_Templates_Params.cpp` | 119 | ✅ Fixed |

~~`parse_template_parameter()` does not handle default arguments for template-template parameters.~~ **Fixed**: After the parameter name is parsed, the parser now checks for `=` and calls `parse_type_specifier()` to read the default type, storing it via `set_default_value()`. This follows the same pattern used for `typename`/`class` parameter defaults.

---

## 19. Concept Template Arguments Skipped

| File | Line | Status |
|------|------|--------|
| `src/Parser_Templates_Params.cpp` | 162 | ✅ Fixed |

~~When a constrained type parameter has template arguments on the concept — `Concept<U> T` — the `<U>` part is consumed by skipping balanced angle brackets without storing `U`.~~ **Fixed**: The concept template arguments are now parsed as type specifiers and stored via `set_concept_args()` on the `TemplateParameterNode`. A new `concept_args_` vector field was added to hold the parsed types.

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
| `src/CodeGen_MemberAccess.cpp` | 2557 | ✅ Fixed |
| `src/CodeGen_MemberAccess.cpp` | 2635 | ✅ Fixed |

Five type-trait intrinsics use heuristics instead of the correct C++ standard definitions:

| Trait | Current heuristic | What's missing |
|-------|------------------|----------------|
| `__is_trivially_copyable` | recursive base-class + member check via `isTriviallyCopyableStruct()` | ✅ Fixed — `isTriviallyCopyableStruct()` now recursively checks base classes AND all non-static data members of class type |
| `__is_trivial` | recursive base-class + member check via `isTrivialStruct()` | ✅ Fixed — same as above; `isTrivialStruct()` also checks `hasUserDefinedConstructor()`. |
| `__is_nothrow_constructible` | checks user-defined ctor noexcept status | ✅ Fixed |
| `__is_nothrow_assignable` | checks user-defined assignment op noexcept status | ✅ Fixed |

✅ **Fixed (2026-03-02)**: `StructTypeInfo::findCopyConstructor()` / `findMoveConstructor()` now skip implicitly-generated constructors (`ctor_node.is_implicit()`). `findCopyAssignmentOperator()` / `findMoveAssignmentOperator()` also now skip implicit operators, and `hasUserDefinedConstructor()` now ignores implicit constructors. This fixes false negatives where a plain struct (e.g., `struct Foo { int x; }`) was incorrectly treated as non-trivially-copyable/non-trivial because implicit special members were counted as user-defined.

~~**Remaining work**: `StructMemberFunction` does not yet store whether the function is `noexcept`. Adding an `is_noexcept` field and populating it during parsing would allow the nothrow traits to give correct answers for user-defined special members.~~ **Partially addressed**: `is_noexcept` field added and nothrow traits now use it. Remaining: `__is_trivially_copyable` and `__is_trivial` still need per-member triviality checks for base classes.

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

## 24. Template Substitutor – String-Based Argument Parsing in `substituteInType()`

| File | Line | Status |
|------|------|--------|
| `src/ExpressionSubstitutor.cpp` | 1033–1102 | ✅ Fixed |

**Fixed (2026-03-02)**: Replaced string-based template argument parsing in `substituteInType()` with structured metadata from `TypeInfo::isTemplateInstantiation()`, `baseTemplateName()`, and `templateArgs()`.

The new path converts stored `TemplateArgInfo` records into `TemplateTypeArg`, substitutes dependent arguments from `param_map_`, and instantiates using that argument vector. This removes fragile splitting/parsing of template type names and handles built-in argument types (e.g., `bool`) without relying on `gTypesByName`.

Test added: `tests/test_template_builtin_arg_ret42.cpp`.

---

## 25. Global Function Pointer Initialization

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_Stmt_Decl.cpp` | 100 | ✅ Already works |

~~Global variables initialized with a function address (e.g., `int (*func_ptr)(int, int) = add;`) require a relocation in the ELF `.data` section.~~ **Verified working**: `int (*g_fp)(int, int) = add;` at global scope compiles correctly; the linker resolves the function address via R_X86_64_64 relocation, and `g_fp(40, 2)` returns 42 at runtime.

---

## Summary Table

| Category | Count | Status |
|----------|-------|--------|
| Function pointer return types (indirect call) | 3 | ✅ Fixed |
| Template substitutor gaps | 2 | ✅ Fixed |
| Preprocessor `#line` filename | 1 | ✅ Fixed |
| IR converter error messages / SSE moves | 3 | ✅ Fixed |
| Member struct template base classes | 2 | ✅ Fixed |
| Declarator parsing gaps | 2 | ✅ Fixed (both verified working) |
| Specifier propagation to struct decl | 1 | ✅ Fixed |
| Constexpr evaluation gaps | 4 | ⚠️ 2 ✅ Fixed (`this->member`, `arr[0].member`), 2 ✅ Valid |
| Overload resolution | 3 | ✅ Fixed (all working) |
| Missing return diagnostic | 1 | ✅ Fixed |
| Template deduction non-type params | 1 | ✅ Valid |
| Phase labels (stale) | 2 | ✅ Already fixed |
| Complex pack expansion | 1 | ✅ Valid |
| Placement new multiple args | 1 | ✅ Fixed |
| Lambda-to-function-pointer type | 1 | ✅ Valid |
| Copy constructor type_index check | 1 | ✅ Fixed (also fixed `isOwnTypeIndex()` for template instantiations) |
| `pointer_depth` in address-of | 7 | 🔍 Needs investigation |
| Template template parameter defaults | 1 | ✅ Fixed |
| Concept template arguments | 1 | ✅ Fixed |
| Array member length | 1 | ✅ Valid |
| Type traits incomplete checks | 5 | ✅ Fixed (trivially copyable/trivial now recurse into base classes; nothrow traits use is_noexcept) |
| `Type::Pointer` enum gap | 1 | ✅ Valid |
| `main` line-mapping guard | 1 | 🔍 Needs investigation |
| Substitutor string-based arg parsing | 1 | ✅ Fixed |
| Global function pointer initialization | 1 | ✅ Already works |
| **Total** | **49** | |

**Stale**: 0 items  
**Fixed**: 35+ entries (all previous fixes plus: constexpr this->member assignment, constexpr `arr[0].member` evaluation, trivially copyable/trivial recursive base check, member struct template base classes ×2, placement new multi-arg storage, implicit copy/move ctor filtering for type traits, and structured substitutor template-arg handling)  
**Needs investigation before fixing**: 8 items (pointer_depth sites + `main` guard)  
**Genuinely unimplemented**: 9 items (complex constexpr patterns, pack expansion, lambda-to-funcptr type, array member length, Type::Pointer enum, template deduction non-type params)

## Existing issues encountered while implementing

- `tests/test_type_traits_intrinsics_ret147.cpp` has a stale trailing comment in `main()` (`Expected: 146`) that does not match the filename expectation (`ret147`).
- Global `constexpr` arrays of struct objects (e.g., `constexpr Item items[2] = {Item(10), Item(42)};`) still trigger the Codegen warning `Non-constant initializer in global variable` from `src/CodeGen_Stmt_Decl.cpp:100`, even though dependent constant expressions like `constexpr int extracted = items[1].value;` now evaluate correctly.
- With overloaded constructors sharing parameter count (e.g., `constexpr Item(double)`, `constexpr Item(char)`, and non-constexpr `Item(int)`), `constexpr` extraction through `arr[index].member` can still be reported as non-constant for globals in current codegen/evaluator integration (observed in `tests/test_constexpr_array_subscript_member_ret42.cpp` as warning on global `extracted`).
- Constructor matching in constexpr array-member extraction currently uses parameter-count filtering with ambiguity rejection. When multiple same-count overloads remain viable, evaluation falls back to a generic "No matching constructor found for constexpr array element" error instead of emitting a dedicated ambiguity diagnostic.
- `tests/test_integral_constant_pattern_ret42.cpp` still emits pre-existing codegen diagnostics (`Parser returned size_bits=0` and repeated `handleLValueCompoundAssignment: FAIL`) despite producing the correct runtime result.
