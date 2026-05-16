# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) Variable-template template-id fallback still treated as callable in dependent static_assert

- **Symptom**: During `<string_view>` instantiation, expressions such as `is_same_v<...>` can fail with  
  `Identifier is not a function or callable object: is_same_v`.
- **Root cause**: When variable-template instantiation fails in dependent contexts, the evaluator still falls
  back to call-expression semantics instead of preserving a dependent value-template expression.
- **Affected path**: `ExpressionSubstitutor::substituteFunctionCallImpl` / variable-template instantiation +
  constexpr call-evaluation fallback.
- **Impact**: Blocks portions of libstdc++ headers that rely on variable templates in dependent checks.

## 2) Two-phase lookup for member function template bodies — **FIXED (2026-05-15)**

Two-phase name lookup (C++20 [temp.res]/9) is now applied inside member function template bodies.
Non-dependent calls inside `template <typename U> T Wrapper<T>::foo(U)` bodies now resolve against
definition-time overload sets, consistent with free function template bodies.

- **Fixed in**: `src/Parser_Templates_Inst_MemberFunc.cpp` — `instantiate_member_function_template_core`
  now sets `phase1_cutoff_line_`, `phase1_cutoff_file_idx_`, and
  `current_template_definition_lookup_context_` before `parse_function_body()`.
- **Regression guard**: `filterPhase1OrdinaryFunctionOverloads` is no longer called from
  `lookupMemberFunctionTemplateCandidatesForInstantiation` — member lookup within a class has
  no phase1 cutoff (all class members are mutually visible).
- **Regression test**: `tests/test_template_two_phase_member_func_template_ret42.cpp`

---

## 3) Windows UCRT `va_start` assertion macro still trips expression parsing in `<limits>` include path

- **Symptom**: On Windows/MSVC STL, `tests/std/test_std_limits.cpp` currently stops in
  `corecrt_wstdio.h:486` at
  `__vcrt_assert_va_start_is_not_reference<decltype(_Locale)>()` with
  `Unexpected keyword in expression context`.
- **Root cause**: Not fully reduced yet, but the failure sits in the UCRT
  `va_start` assertion macro path and likely involves parsing unevaluated
  `decltype(...)` inside a macro-expanded comma-expression/cast wrapper.
- **Affected path**: Windows standard-header include chain for `<limits>` via
  `<cwchar>` / `<cstdio>` / `stdio.h` / `corecrt_wstdio.h`.
- **Impact**: Blocks deeper Windows/MSVC `<limits>` parsing and makes local
  performance analysis on this header a "time to first hard parse error"
  measurement instead of a full successful compile.

---

## 4) Pointer-NTTP full specialization dispatch: second lookup produces wrong arg type

- **Symptom**: Given `template <int* P> struct Tag { static int value() { return -1; } };`
  `template <> struct Tag<&ga> { static int value() { return 10; } };`, calling
  `Tag<&ga>::value()` in `main()` returns `-1` (invokes the primary template) instead of `10`.
  The specialization IS correctly instantiated and both symbols appear in the object file.

- **Root cause (confirmed via debug instrumentation)**:
  There are two distinct lookup paths for `Tag<&ga>`:
  1. During parsing of `template <> struct Tag<&ga>`: `&ga` is evaluated to
     `ObjectPointer(nativeTypeIndex(Int), "ga", 0)` → `SpecializationKey` hash matches →
     `lookupExactSpecialization` returns the spec node ✓
  2. During parsing of `Tag<&ga>::value()` in `main()`: `&ga` evaluates to a plain
     `TemplateTypeArg(0, Int)` (value=0, `has_typed_value_identity=false`) → different hash →
     `lookupExactSpecialization` returns `nullopt` → generic template is instantiated with
     mangled name `Tag$00000a1837ced158` instead of `Tag$00a3b7d5a86c5df9` → `main` calls
     the wrong symbol.

  The second path fails to produce an `ObjectPointer` identity for the `&ga` argument. This
  is likely because the expression-side template-argument classification in `main` context
  either calls `parse_explicit_template_arguments()` without template params (skipping
  `classifyExplicitTemplateArgumentsAgainstParameters`), or `try_evaluate_constant_expression`
  returns an `EvalResult` without `pointer_to_var` set in that path, causing fallback to
  `TemplateTypeArg::makeValue(0, Int)`.

- **Affected paths**:
  - `src/Parser_Expr_PrimaryExpr.cpp` — second call to `parse_explicit_template_arguments` /
    `materializePrimaryTemplateOwnerForLookup` (line ~1461)
  - `src/Parser_Templates_Params.cpp` — `makeValueArgForSyntax` / `classifyExplicitTemplateArgumentsAgainstParameters`
  - `src/TemplateRegistry_Registry.h` — `lookupExactSpecialization`
  - `src/Parser_Templates_Function.cpp` — `makeConstantValueFromEvalResult` (creates `ObjectPointer`)

- **Recommended fix**: Ensure that wherever `Tag<&ga>` template args are parsed for the member
  access / function call context, the path goes through `parse_explicit_template_arguments(primary_template_params)`
  (with template params provided) and `classifyExplicitTemplateArgumentsAgainstParameters` is
  called so `&ga` is evaluated with the declared `int*` type, giving `pointer_to_var.isValid()=true`
  and thus `ObjectPointer` identity via `makeConstantValueFromEvalResult`.

- **Impact**: All pointer-NTTP full specializations where the specialization adds/changes member
  functions are silently not dispatched to; the generic template is called instead.

- **Regression test (pending)**: `tests/test_nttp_ptr_specialization_ret0.cpp`


- **Symptom**: Parser warnings like `Parameter 1 type mismatch in out-of-line template member ...` remain for
  valid declarations/definitions that differ only by dependent spelling/aliases.
- **Root cause**: `validate_signature_match` compares low-level type fields (`type_index`, category, pointer/ref)
  too rigidly for dependent contexts and does not use a more canonicalized/dependent-aware type equivalence.
- **Affected path**: `Parser::validate_signature_match`, invoked from out-of-line template member parsing.
- **Impact**: Noisy diagnostics and increased risk of picking/validating against suboptimal overload candidates.
