# TODO / FIXME Analysis

**Date**: 2026-03-01 (last updated 2026-03-09)
**Total items found**: 56 (44 TODO + 4 FIXME/minor + 1 discovered + 11 newly fixed)
**Search scope**: `src/**/*.cpp`, `src/**/*.h`

---

## 1. Codegen – Function Pointer Call Return Types ✅ Fixed
`src/IrGenerator_Call_Indirect.cpp` lines 399, 539, 603 — Added `std::optional<FunctionSignature>` to `StructMember`; all three codegen sites read the stored return type instead of hardcoding `Void`/`Int`.

---

## 2. Template Expression Substitutor ✅ Fixed
`src/ExpressionSubstitutor.cpp` lines 1069, 1123 — Multi-argument base-class substitution now splits on commas respecting angle-bracket depth. `ensureTemplateInstantiated()` now delegates to `try_instantiate_class_template()`.

---

## 3. Preprocessor – `#line` Directive Filename ✅ Fixed
`src/FileReader_Macros.cpp:1291` — Filename interned into `file_paths_` for stable lifetime; `filestack_.top().file_name` updated accordingly.

---

## 4. IR Converter – Error Handling and SSE Moves ✅ Fixed
`src/IRConverter_ConvertMain.h` (formerly `src/IRConverter_Conv_CorePrivate.h`) — Error messages now include variable names. Float reg-to-reg moves now emit proper `MOVSS`/`MOVSD` via `emitFloatMovRegToReg()`.

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
| `src/IrGenerator_Stmt_Decl.cpp` | 203 | ✅ Fixed |

- **FunctionOrVar.cpp:1005** ✅ Fixed — Replaced `is_struct_init_list` guard with a recursive `validate_single` lambda that iterates `InitializerListNode` elements individually. Now `constinit int arr[] = {1,2,3}` correctly validates each element, and `constinit Point p = {runtime_val, 2}` correctly fails when the element is non-constant. Tests: `test_constinit_aggregate_ret42.cpp`, `test_constinit_nonconstant_struct_fail.cpp`.
- **Members.cpp:202** ✅ Fixed — `this->x = value` assignments in constexpr member functions now update `bindings[member_name]`. Test: `test_constexpr_this_member_ret42.cpp`.
- **Members.cpp:1473** ✅ Fixed — `arr[0].member` in constexpr context now works via `evaluate_array_subscript_member_access()`. Test: `test_constexpr_array_subscript_member_ret42.cpp`.
- **Core.cpp:1050** ✅ Fixed — Constexpr functor `operator()` calls now materialise member bindings and evaluate the operator body. Test: `test_constexpr_functor_call_ret42.cpp`.
- **Members.cpp:1144** ✅ Fixed — `evaluate_member_access` now handles aggregate-initialized (brace-init) constexpr structs via direct member search. Previously, `constexpr Point p = {10, 32}; constexpr int s = p.x + p.y;` failed. Now scalar members are found directly without evaluating sibling struct-type elements. Test: `test_constexpr_aggregate_member_access_ret42.cpp`.
- **Members.cpp:1302** ✅ Fixed — `evaluate_nested_member_access` now handles aggregate-initialized base structs. Previously, `constexpr Outer o = {{20}, 22}; constexpr int r = o.inner.val + o.extra;` failed. Now handles nested `InitializerListNode` members recursively. Test: `test_constexpr_nested_aggregate_member_ret42.cpp`.
- **IrGenerator_Stmt_Decl.cpp:203** ✅ Fixed — Global struct aggregate initialization with nested struct members (e.g., `Line l = {{1,2},{3,4}}`) now correctly fills bytes for nested members using a recursive `fillStructData` lambda. Test: `test_nested_struct_global_init_ret10.cpp`.

### Additional constexpr progress since the original analysis

- ✅ local constexpr object member reads through locals, including nested reads like `obj.inner.value` and member-array reads like `obj.data[0]`
- ✅ inferred-size local arrays in constexpr functions, including aggregate-array element member reads like `items[0].value`
- ✅ regression coverage confirming already-supported local aggregate-array nested/member-array compositions and loop-driven reads like `sum += arr[i]` / `sum += items[i].value`
- ✅ shared constructor-backed object materialization across constexpr paths, so constructor-built object state is reused consistently for member access, member-function/callable evaluation, and object extraction
- ✅ brace-init now prefers constructor calls for types with user-declared constructors instead of silently falling back to aggregate/member-wise initialization
- ✅ straightforward constexpr constructor-body expression assignments and simple member-to-member dependencies, e.g. `value = input + 2;` and `y = x + 2;`
- ✅ doc update in `docs/CONSTEXPR_LIMITATIONS.md` clarifying that simple constructor-body member assignments are supported, while more complex constructor-body execution remains a harder follow-up area
- **Additional regression tests added so far**:
  - `test_constexpr_local_object_nested_member_access_ret0.cpp`
  - `test_constexpr_local_object_array_member_access_ret0.cpp`
  - `test_constexpr_local_unsized_array_ret0.cpp`
  - `test_constexpr_local_unsized_aggregate_array_member_access_ret0.cpp`
  - `test_constexpr_local_aggregate_array_nested_member_access_ret0.cpp`
  - `test_constexpr_local_aggregate_array_member_array_access_ret0.cpp`
  - `test_constexpr_local_array_for_loop_sum_ret0.cpp`
  - `test_constexpr_local_aggregate_array_for_loop_member_sum_ret0.cpp`
  - `test_constexpr_constructor_body_member_assignment_ret0.cpp`
  - `test_constexpr_constructor_body_multiple_member_assignments_ret0.cpp`
  - `test_constexpr_constructor_body_expression_assignment_ret0.cpp`
  - `test_constexpr_constructor_body_member_dependency_ret0.cpp`

---

## 9. Overload Resolution ✅ Fixed
`src/OverloadResolution.h` lines 684, 700; `src/SymbolTable.h:484` — `findBinaryOperatorOverload()` and `lookup_function()` now perform two-phase exact-then-fallback matching. Free-function operator overloads were already found.

---

## 10. Missing Return Statement Diagnostic ✅ Fixed
`src/IrGenerator_Visitors_Decl.cpp:860` — Emits a `Codegen Warning` when a non-void function's last instruction is not a return.

---

## 11. Template Argument Deduction – Non-Type Parameters ✅ Fixed (2026-03-03)
`src/Parser_Templates_Inst_Deduction.cpp:698` — Pre-deduction pass now matches function parameter types to call-site argument types to deduce both type and non-type template params from struct template arguments. Test: `test_nontype_template_deduction_ret5.cpp`.

---

## 12. Template Instantiation Phase Labels ✅ Already fixed
`src/IrGenerator_Visitors_TypeInit.cpp` lines 133, 138 — Comments already explain why the early return is correct. No action needed.

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
`src/Parser_Expr_PrimaryUnary.cpp:977`, `src/Parser_Expr_QualLookup.cpp:1243`, `src/Parser_Statements.cpp:888`, `src/IrGenerator_Expr_Conversions.cpp:1036` — Unary `+` on a captureless lambda now produces a `Type::FunctionPointer` `TypeSpecifierNode` with the matching signature. Captureful lambdas still error. Test: `test_lambda_plus_function_pointer_ret42.cpp`.

---

## 16. Copy Constructor Detection – Type Index Check ✅ Fixed
`src/AstNodeTypes.cpp:647` — All four special-member finders (`findCopyConstructor`, `findMoveConstructor`, `findCopyAssignmentOperator`, `findMoveAssignmentOperator`) now use `isOwnTypeIndex()` to reject mismatched types.

---

## 17. `pointer_depth` in Address-Of Operations ✅ Verified (no change)
Eight sites across `IrGenerator_Expr_Operators.cpp`, `IrGenerator_Visitors_Namespace.cpp`, `IrGenerator_Stmt_Decl.cpp`, `IrGenerator_NewDeleteCast.cpp`, `IrGenerator_Lambdas.cpp`, `AstToIr.h` — `handleAddressOf()` does not read `pointer_depth`; setting it to `0` is a safe no-op.

---

## 18. Template Template Parameter Default Arguments ✅ Fixed
`src/Parser_Templates_Params.cpp:119` — Default type argument after `=` is now parsed and stored via `set_default_value()`.

---

## 19. Concept Template Arguments Skipped ✅ Fixed
`src/Parser_Templates_Params.cpp:162` — `<U>` on constrained type parameters is now parsed as type specifiers and stored via `set_concept_args()`.

---

## 20. Array Member Length in Code Generation ✅ Fixed
`src/IrGenerator_MemberAccess.cpp:447` — Element size now computed from `array_dimensions` product instead of `get_type_size_bits()`; `type_index` propagated for chained member access. Test: `test_struct_member_array_elem_ret42.cpp`.

---

## 21. Type Traits – Incomplete Checks

| File | Line | Status |
|------|------|--------|
| `src/IrGenerator_MemberAccess.cpp` | 2360 | ✅ Valid |
| `src/IrGenerator_MemberAccess.cpp` | 2372 | ✅ Valid |
| `src/IrGenerator_MemberAccess.cpp` | 2380 | ✅ Valid |
| `src/IrGenerator_MemberAccess.cpp` | 2557 | ✅ Fixed |
| `src/IrGenerator_MemberAccess.cpp` | 2635 | ✅ Fixed |

`__is_trivially_copyable` and `__is_trivial` (lines 2360–2380) still use heuristics. Lines 2557/2635: `is_noexcept` field added; `__is_nothrow_constructible` and `__is_nothrow_assignable` now use it. Implicit special members are now correctly skipped by `findCopyConstructor`/`hasUserDefinedConstructor`.

---

## 22. Pointer Type in `Type` Enum ✅ Fixed (2026-03-09)

| File | Line | Status |
|------|------|--------|
| `src/IrGenerator_Call_Direct.cpp` | 932 | ✅ Fixed |

Rather than adding a new `Type::Pointer` enum value, the fix preserves pointer metadata through the existing IR payloads:

- `src/IROperandHelpers.h` now interprets the optional 4th IR operand as `pointer_depth` for non-struct values.
- `src/IrGenerator_Call_Direct.cpp` and `src/IrGenerator_Expr_Operators.cpp` re-apply parameter `TypeSpecifierNode` metadata when building `TypedValue` call/default arguments, so array-to-pointer decay keeps pointer depth, `type_index`, and qualifiers.
- `src/IrGenerator_MemberAccess.cpp` now reduces pointer depth correctly during pointer subscripting (`int** p; p[i]` yields `int*`, not `int`).

This fixes the concrete failure mode behind the item: array arguments decaying to pointers across calls now retain enough type information for later IR stages, including multi-level pointer indexing. Tests: `test_array_decay_pointer_metadata_ret0.cpp`.

---

## 23. `main` Special-Case in Line Mapping ✅ Fixed
`src/IRConverter_ConvertMain.h` (formerly `src/IRConverter_Conv_VarDecl.h`) — `main` exclusion guard removed; `addLineMapping()` now fires for all functions.

---

## 24. Template Substitutor – String-Based Argument Parsing ✅ Fixed
`src/ExpressionSubstitutor.cpp:1033–1102` — Replaced fragile string-splitting with structured `TemplateArgInfo` metadata; handles built-in arg types without `gTypesByName`. Test: `test_template_builtin_arg_ret42.cpp`.

---

## 25. Global Function Pointer Initialization ✅ Already works
`src/IrGenerator_Stmt_Decl.cpp:100` — `int (*g_fp)(int, int) = add;` at global scope compiles and links correctly via R_X86_64_64 relocation.

---

## Summary

| Status | Count |
|--------|-------|
| ✅ Fixed | 55 |
| ✅ Verified / Already works | 10 |
| ✅ Valid (open) | 1 |
| **Total** | **52** (+ several post-analysis fixes) |

**Open items**: `__is_trivially_copyable`/`__is_trivial` full correctness (#21). All item #8 constexpr evaluation gaps have been resolved.

---

## Known issues encountered during implementation (updated 2026-03-05)

- Constexpr array-member extraction still falls back to an error (instead of an ambiguity diagnostic) when multiple same-arity constructors are viable. The larger brace-init-vs-constructor selection bug for user-declared constructors is fixed, but same-arity ambiguity remains a follow-up.
- `constinit` on brace-initialized callable objects (e.g., `constexpr Add add{}; constinit int x = add(1,2);`) ✅ Fixed – `evaluate_callable_object` now handles `InitializerListNode` initializers. `evaluate_member_function_call` delegates `operator()` calls to `evaluate_callable_object`. Tests: `test_constinit_callable_ret42.cpp`, `test_constinit_callable_ctor_ret42.cpp`.
- `ConstructorCallNode` wrapped in `ExpressionNode` not recognised in constexpr evaluator ✅ Fixed – added `extract_constructor_call()` helper that unwraps both direct and `ExpressionNode`-wrapped `ConstructorCallNode`s.
- Constexpr `evaluate_callable_object()` rejects ambiguous same-arity `operator()` overloads rather than resolving by type.
- `tests/test_integral_constant_pattern_ret42.cpp` emits pre-existing `Parser returned size_bits=0` / `handleLValueCompoundAssignment: FAIL` diagnostics despite producing the correct runtime result.
- `tests/test_namespace_template_specialization_ret42.cpp` ✅ Fixed – mangled name mismatch resolved; test now links and passes.

---

## 26. Default Function Arguments ✅ Fixed (2026-03-05)
`src/OverloadResolution.h`, `src/SymbolTable.h`, `src/Parser_Expr_PrimaryExpr.cpp` — Overload resolution now accounts for trailing default parameter values. When a call provides fewer arguments than parameters, the remaining default argument expressions are filled into the FunctionCallNode at parse time. Added `countMinRequiredArgs()` helper. Test: `test_default_function_args_ret42.cpp`.

---

## 27. Compound Assignment on Global/Static Variables ✅ Fixed (2026-03-05)
`src/IrGenerator_Expr_Operators.cpp` — Compound assignment operators (`+=`, `-=`, `*=`, etc.) on global variables and static local variables now generate proper `GlobalLoad → arithmetic → GlobalStore` IR. Previously only simple assignment (`=`) was handled; compound ops silently lost the store. Test: `test_compound_assign_global_ret42.cpp`.

---

## 28. Range-Based For Loop with Unsized Arrays ✅ Fixed (2026-03-05)
`src/IrGenerator_Stmt_Control.cpp` — Range-based for loops over unsized arrays (`int arr[] = {1,2,3}`) now infer the array size from the initializer list. Previously, `visitRangedForArray` required `array_size()` which returned `nullopt` for unsized arrays. Test: `test_range_for_unsized_array_ret42.cpp`.

---

## 29. Assignment Through Reference-Returning Methods ✅ Verified / Already works (2026-03-09)
`src/IrGenerator_Call_Direct.cpp` and `src/IrGenerator_Call_Indirect.cpp` already mark reference-returning calls as indirect lvalues/xvalues, so assignments store through the referenced target instead of a temporary. Rebuilt the compiler and verified `h.getRef() = 40; h.getRef() += 2;` with tests `ref_return_member_param_ret42.cpp` and `test_ref_return_member_field_assignment_ret42.cpp`.

---

## 30. Default Arguments for Member Functions ✅ Fixed (2026-03-06)
`src/IrGenerator_Call_Indirect.cpp` — Member function calls with omitted trailing default arguments now work. Default argument fill-in added at the CodeGen level after argument processing, using the resolved function declaration's parameter list. Test: `test_default_args_extended_ret42.cpp`.

---

## 31. Default Arguments for Template Functions ✅ Fixed (2026-03-06)
`src/Parser_Templates_Inst_Deduction.cpp`, `src/IrGenerator_Call_Direct.cpp` — Template function instantiation now preserves default argument values from the original template declaration when creating substituted parameter nodes. CodeGen-level default fill-in also added for direct calls. Test: `test_default_args_extended_ret42.cpp`.

---

## 32. Nested Struct Aggregate Init in generateDefaultStructArg ✅ Fixed (2026-03-06)
`src/IrGenerator_Expr_Operators.cpp`, `src/IrGenerator_Visitors_Decl.cpp` — When a default argument is a braced initializer list whose members include nested struct initializers (e.g., `Outer o = {10, {12, 20}}`), the `generateDefaultStructArg` helper and the aggregate-init path of `generateConstructorCallIr` now recursively construct nested sub-aggregates. Previously, the nested `InitializerListNode` was incorrectly cast to `ExpressionNode`, causing `bad_any_cast`. Added `store_value_set` guard to prevent emitting `MemberStoreOp` with a default-initialised `IrValue`. Test: `test_nested_struct_default_arg_ret42.cpp`.

---

## 33. Silent Drop in generateDefaultStructArg Error Path ✅ Fixed (2026-03-06)
`src/IrGenerator_Call_Direct.cpp`, `src/IrGenerator_Call_Indirect.cpp` — When `generateDefaultStructArg` returns `nullopt` (type lookup failure), the argument was silently dropped with no diagnostic. Now emits a `FLASH_LOG(Codegen, Error, ...)` message so failures are visible in logs.

---

## 34. 9–16 Byte Struct Caller/Callee ABI Mismatch ✅ Fixed (2026-03-06, revalidated 2026-03-18)
`src/IRConverter_ConvertMain.cpp` / `src/IRConverter_ConvertMain.h` — On SysV AMD64, non-variadic 9–16 byte by-value structs are now handled with the ABI-mandated two-register convention on both the caller and callee sides. The call lowering, stack-overflow path, constructor-call path, and function-declaration prologue all recognize INTEGER-classified 9–16 byte structs via `isTwoRegisterStructRaw(...)`, suppress the by-address fallback in `shouldPassStructByAddress(...)`, and materialize register-passed values directly in the callee frame. Revalidated with external-clang interop coverage for both 12-byte (`Big3`) and 16-byte (`Big4`) structs in both directions, including register and stack-overflow cases. Test: `test_external_abi.cpp` + `test_external_abi_helper.c`.

---

## 35. const& Struct Default Arguments Pass Value Instead of Pointer ✅ Fixed (2026-03-06)
`src/IrGenerator_Call_Direct.cpp`, `src/IrGenerator_Call_Indirect.cpp` — When a struct parameter declared as `const T&` has a braced-init default (e.g., `void f(const Point& p = {1, 2})`), the generated default argument TypedValue was not marked as a reference. The caller therefore passed the struct bytes by value rather than by pointer, causing the callee (which dereferences the pointer) to segfault. Both the `InitializerListNode` and `ExpressionNode` default fill-in paths now check `param_type.is_reference()` and set `ref_qualifier = ReferenceQualifier::LValueReference` on the resulting TypedValue. Test: `test_struct_const_ref_args_ret42.cpp`.

---

## 36. SysV AMD64 Variadic Struct Argument ABI ✅ Fixed (2026-03-06)
`src/IRConverter_ConvertMain.h` (formerly `src/IRConverter_Emit_CompareBranch.h` and `src/IRConverter_Conv_Calls.h`) — For variadic calls on Linux with 9–16 byte struct arguments (e.g., `Point3D{x,y,z,w}` = 16 bytes passed to `sum_points3d(int count, ...)`), the caller was using the pointer convention (passing `&p` via LEA). The callee's `va_arg` reads from the register save area (not through a pointer), so it was reading garbage. Fixed by restoring the two-register convention exclusively for Linux variadic calls: `isTwoRegisterStruct(arg, is_variadic_call=true)` returns `true` for 9–16 byte structs in variadic context; `shouldPassStructByAddress(arg, is_two_register_struct)` returns `false` when the two-register path applies; and the register-passing loop checks `is_potential_two_reg_struct` before `shouldPassStructByAddress` to prevent the pointer path from overriding. Non-variadic struct passing (pointer convention) is unaffected. Test: `test_va_large_struct_ret0.cpp`.

---

## 37. `if constexpr` in Template Bodies — Long-Term Conformance Follow-up
`src/Parser_Templates_Inst_Deduction.cpp`, `src/Parser_Templates_Substitution.cpp`, `src/Parser_Expr_ControlFlowStmt.cpp` — Current work now prefers substituting an already-parsed template body AST before falling back to token-level body re-parsing, which reduces reliance on parser-time dead-branch skipping. The long-term standards-aligned solution is to preserve template function bodies as AST consistently, evaluate dependent `if constexpr` conditions during instantiation/substitution, and instantiate only the selected substatement once the condition becomes non-dependent. Token-level branch skipping during body re-parse should remain only as a compatibility fallback for bodies that still cannot be represented faithfully as AST during initial parse.

---

## 38. Preprocessor Conformance Follow-Ups (Open)

- Preserve declaration-suffix assembler rename metadata for variables declared with `__asm("symbol")` / `__asm__("symbol")`; function declarations now retain the requested external symbol.
- Stop defining both `__GNUC__` and `_MSC_VER` simultaneously once header-compatibility strategy is in place for both library families.
- Only raise `__cpp_consteval`, `__cpp_constexpr_dynamic_alloc`, or a higher `__cpp_constexpr` level again after the underlying constexpr/consteval features are actually implemented.
- Re-evaluate `__cpp_exceptions` once exception handling is complete enough for standard-library throw paths. ✅ **Done** — `__cpp_exceptions=199711L` is now defined; basic throw/catch/unwind works on ELF.
