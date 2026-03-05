# TODO / FIXME Analysis

**Date**: 2026-03-01 (last updated 2026-03-05)
**Total items found**: 49 (44 TODO + 4 FIXME/minor + 1 discovered)
**Search scope**: `src/**/*.cpp`, `src/**/*.h`

---

## 1. Codegen – Function Pointer Call Return Types ✅ Fixed
`src/CodeGen_Call_Indirect.cpp` lines 399, 539, 603 — Added `std::optional<FunctionSignature>` to `StructMember`; all three codegen sites read the stored return type instead of hardcoding `Void`/`Int`.

---

## 2. Template Expression Substitutor ✅ Fixed
`src/ExpressionSubstitutor.cpp` lines 1069, 1123 — Multi-argument base-class substitution now splits on commas respecting angle-bracket depth. `ensureTemplateInstantiated()` now delegates to `try_instantiate_class_template()`.

---

## 3. Preprocessor – `#line` Directive Filename ✅ Fixed
`src/FileReader_Macros.cpp:1291` — Filename interned into `file_paths_` for stable lifetime; `filestack_.top().file_name` updated accordingly.

---

## 4. IR Converter – Error Handling and SSE Moves ✅ Fixed
`src/IRConverter_Conv_CorePrivate.h` lines 504, 726, 1067 — Error messages now include variable names. Float reg-to-reg moves now emit proper `MOVSS`/`MOVSD` via `emitFloatMovRegToReg()`.

---

## 5. Template Class – Member Struct Base Class Parsing ✅ Fixed
`src/Parser_Templates_Class.cpp` lines 4640, 5007 — Added `parse_member_struct_template_base_class_list()` helper; deferred/concrete base classes are now recorded instead of silently discarded. Tests: `test_item5b_ret42.cpp`, `test_member_struct_template_concrete_base_ret42.cpp`.

---

## 6. Declarator Parsing ✅ Fixed
`src/Parser_Decl_DeclaratorCore.cpp` lines 898, 993 — Parenthesized declarators already worked. `extern "C"` linkage now threaded through all `parse_postfix_declarator()` call sites.

---

## 7. `parse_struct_declaration()` – Missing Specifier Propagation ✅ Fixed
`src/Parser_Decl_FunctionOrVar.cpp:28` — Added `parse_struct_declaration_with_specs(bool, bool)` so pre-parsed `constexpr`/`inline` flags reach trailing variable declarations.

---

## 8. Constexpr Evaluation Gaps

| File | Line | Status |
|------|------|--------|
| `src/Parser_Decl_FunctionOrVar.cpp` | 1005 | ✅ Fixed |
| `src/ConstExprEvaluator_Members.cpp` | 202 | ✅ Fixed |
| `src/ConstExprEvaluator_Members.cpp` | 1473 | ✅ Fixed |
| `src/ConstExprEvaluator_Core.cpp` | 1050 | ✅ Fixed |
| `src/ConstExprEvaluator_Members.cpp` | 1144 | ✅ Fixed |
| `src/ConstExprEvaluator_Members.cpp` | 1302 | ✅ Fixed |
| `src/CodeGen_Stmt_Decl.cpp` | 203 | ✅ Fixed |

- **FunctionOrVar.cpp:1005** ✅ Fixed — Replaced `is_struct_init_list` guard with a recursive `validate_single` lambda that iterates `InitializerListNode` elements individually. Now `constinit int arr[] = {1,2,3}` correctly validates each element, and `constinit Point p = {runtime_val, 2}` correctly fails when the element is non-constant. Tests: `test_constinit_aggregate_ret42.cpp`, `test_constinit_nonconstant_struct_fail.cpp`.
- **Members.cpp:202** ✅ Fixed — `this->x = value` assignments in constexpr member functions now update `bindings[member_name]`. Test: `test_constexpr_this_member_ret42.cpp`.
- **Members.cpp:1473** ✅ Fixed — `arr[0].member` in constexpr context now works via `evaluate_array_subscript_member_access()`. Test: `test_constexpr_array_subscript_member_ret42.cpp`.
- **Core.cpp:1050** ✅ Fixed — Constexpr functor `operator()` calls now materialise member bindings and evaluate the operator body. Test: `test_constexpr_functor_call_ret42.cpp`.
- **Members.cpp:1144** ✅ Fixed — `evaluate_member_access` now handles aggregate-initialized (brace-init) constexpr structs via direct member search. Previously, `constexpr Point p = {10, 32}; constexpr int s = p.x + p.y;` failed. Now scalar members are found directly without evaluating sibling struct-type elements. Test: `test_constexpr_aggregate_member_access_ret42.cpp`.
- **Members.cpp:1302** ✅ Fixed — `evaluate_nested_member_access` now handles aggregate-initialized base structs. Previously, `constexpr Outer o = {{20}, 22}; constexpr int r = o.inner.val + o.extra;` failed. Now handles nested `InitializerListNode` members recursively. Test: `test_constexpr_nested_aggregate_member_ret42.cpp`.
- **CodeGen_Stmt_Decl.cpp:203** ✅ Fixed — Global struct aggregate initialization with nested struct members (e.g., `Line l = {{1,2},{3,4}}`) now correctly fills bytes for nested members using a recursive `fillStructData` lambda. Test: `test_nested_struct_global_init_ret10.cpp`.

---

## 9. Overload Resolution ✅ Fixed
`src/OverloadResolution.h` lines 684, 700; `src/SymbolTable.h:484` — `findBinaryOperatorOverload()` and `lookup_function()` now perform two-phase exact-then-fallback matching. Free-function operator overloads were already found.

---

## 10. Missing Return Statement Diagnostic ✅ Fixed
`src/CodeGen_Visitors_Decl.cpp:860` — Emits a `Codegen Warning` when a non-void function's last instruction is not a return.

---

## 11. Template Argument Deduction – Non-Type Parameters ✅ Fixed (2026-03-03)
`src/Parser_Templates_Inst_Deduction.cpp:698` — Pre-deduction pass now matches function parameter types to call-site argument types to deduce both type and non-type template params from struct template arguments. Test: `test_nontype_template_deduction_ret5.cpp`.

---

## 12. Template Instantiation Phase Labels ✅ Already fixed
`src/CodeGen_Visitors_TypeInit.cpp` lines 133, 138 — Comments already explain why the early return is correct. No action needed.

---

## 13. Complex Pack Expansion Rewriting ✅ Fixed

| File | Line | Status |
|------|------|--------|
| `src/Parser_Expr_PrimaryExpr.cpp` | 3306 | ✅ Fixed |

When a pack-expansion argument in a non-template function call contained a non-trivial expression (e.g., `add3(do_abs(args)...)`), the parser logged an error and fell back to copying the unexpanded node. Fix: `exprContainsIdentifier()` helper walks the expression tree to find which pack parameter is referenced; `replacePackIdentifierInExpr()` then expands it for each pack element. Tests: `test_complex_pack_expansion_ret42.cpp`.

---

## 14. Placement `new` – Multiple Arguments ✅ Fixed
`src/Parser_Expr_PrimaryUnary.cpp:361` — `NewExpressionNode` now stores `std::vector<ASTNode> placement_args_`; backward-compatible `placement_address()` returns the first arg. Test: `test_placement_new_multi_args_ret42.cpp`.

---

## 15. Lambda-to-Function-Pointer Conversion Type Node ✅ Fixed
`src/Parser_Expr_PrimaryUnary.cpp:977`, `src/Parser_Expr_QualLookup.cpp:1243`, `src/Parser_Statements.cpp:888`, `src/CodeGen_Expr_Conversions.cpp:1036` — Unary `+` on a captureless lambda now produces a `Type::FunctionPointer` `TypeSpecifierNode` with the matching signature. Captureful lambdas still error. Test: `test_lambda_plus_function_pointer_ret42.cpp`.

---

## 16. Copy Constructor Detection – Type Index Check ✅ Fixed
`src/AstNodeTypes.cpp:647` — All four special-member finders (`findCopyConstructor`, `findMoveConstructor`, `findCopyAssignmentOperator`, `findMoveAssignmentOperator`) now use `isOwnTypeIndex()` to reject mismatched types.

---

## 17. `pointer_depth` in Address-Of Operations ✅ Verified (no change)
Eight sites across `CodeGen_Expr_Operators.cpp`, `CodeGen_Visitors_Namespace.cpp`, `CodeGen_Stmt_Decl.cpp`, `CodeGen_NewDeleteCast.cpp`, `CodeGen_Lambdas.cpp`, `AstToIr.h` — `handleAddressOf()` does not read `pointer_depth`; setting it to `0` is a safe no-op.

---

## 18. Template Template Parameter Default Arguments ✅ Fixed
`src/Parser_Templates_Params.cpp:119` — Default type argument after `=` is now parsed and stored via `set_default_value()`.

---

## 19. Concept Template Arguments Skipped ✅ Fixed
`src/Parser_Templates_Params.cpp:162` — `<U>` on constrained type parameters is now parsed as type specifiers and stored via `set_concept_args()`.

---

## 20. Array Member Length in Code Generation ✅ Fixed
`src/CodeGen_MemberAccess.cpp:447` — Element size now computed from `array_dimensions` product instead of `get_type_size_bits()`; `type_index` propagated for chained member access. Test: `test_struct_member_array_elem_ret42.cpp`.

---

## 21. Type Traits – Incomplete Checks

| File | Line | Status |
|------|------|--------|
| `src/CodeGen_MemberAccess.cpp` | 2360 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2372 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2380 | ✅ Valid |
| `src/CodeGen_MemberAccess.cpp` | 2557 | ✅ Fixed |
| `src/CodeGen_MemberAccess.cpp` | 2635 | ✅ Fixed |

`__is_trivially_copyable` and `__is_trivial` (lines 2360–2380) still use heuristics. Lines 2557/2635: `is_noexcept` field added; `__is_nothrow_constructible` and `__is_nothrow_assignable` now use it. Implicit special members are now correctly skipped by `findCopyConstructor`/`hasUserDefinedConstructor`.

---

## 22. Pointer Type in `Type` Enum ✅ Valid (open)

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

## 23. `main` Special-Case in Line Mapping ✅ Fixed
`src/IRConverter_Conv_VarDecl.h:1733` — `main` exclusion guard removed; `addLineMapping()` now fires for all functions.

---

## 24. Template Substitutor – String-Based Argument Parsing ✅ Fixed
`src/ExpressionSubstitutor.cpp:1033–1102` — Replaced fragile string-splitting with structured `TemplateArgInfo` metadata; handles built-in arg types without `gTypesByName`. Test: `test_template_builtin_arg_ret42.cpp`.

---

## 25. Global Function Pointer Initialization ✅ Already works
`src/CodeGen_Stmt_Decl.cpp:100` — `int (*g_fp)(int, int) = add;` at global scope compiles and links correctly via R_X86_64_64 relocation.

---

## Summary

| Status | Count |
|--------|-------|
| ✅ Fixed | 48 |
| ✅ Verified / Already works | 9 |
| ✅ Valid (open) | 2 |
| **Total** | **49** (+ several post-analysis fixes) |

**Open items**: `Type::Pointer` enum (#22), `__is_trivially_copyable`/`__is_trivial` full correctness (#21). All item #8 constexpr evaluation gaps have been resolved.

---

## Known issues encountered during implementation (updated 2026-03-05)

- Constexpr array-member extraction falls back to an error (instead of an ambiguity diagnostic) when multiple same-arity constructors are viable.
- `constinit` on brace-initialized callable objects (e.g., `constexpr Add add{}; constinit int x = add(1,2);`) ✅ Fixed – `evaluate_callable_object` now handles `InitializerListNode` initializers. `evaluate_member_function_call` delegates `operator()` calls to `evaluate_callable_object`. Tests: `test_constinit_callable_ret42.cpp`, `test_constinit_callable_ctor_ret42.cpp`.
- `ConstructorCallNode` wrapped in `ExpressionNode` not recognised in constexpr evaluator ✅ Fixed – added `extract_constructor_call()` helper that unwraps both direct and `ExpressionNode`-wrapped `ConstructorCallNode`s.
- Constexpr `evaluate_callable_object()` rejects ambiguous same-arity `operator()` overloads rather than resolving by type.
- `tests/test_integral_constant_pattern_ret42.cpp` emits pre-existing `Parser returned size_bits=0` / `handleLValueCompoundAssignment: FAIL` diagnostics despite producing the correct runtime result.
- `tests/test_namespace_template_specialization_ret42.cpp` fails to link: mangled name mismatch for member functions of template specializations declared inside namespaces.
