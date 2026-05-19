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

## 2) MSVC `<limits>` now stops later on `numeric_limits` constexpr member initialization

- **Symptom**: On Windows/MSVC STL, `tests/std/test_std_limits.cpp` now gets past
  the UCRT `__crt_va_start` wrappers but stops later with
  `Fatal error: static constexpr member initializer for 'std::_Num_float_base::has_denorm' is not a constant expression: initializer is unresolved`.
- **Current frontier**: `limits` around the `_Num_base` / `_Num_float_base`
  `static constexpr float_denorm_style has_denorm = denorm_absent/present;`
  members.
- **Root cause**: Not reduced yet. The parser and preprocessor now survive the
  previous `corecrt_wstdio.h` barrier, so the remaining blocker appears to be
  constexpr/sema handling for enum-valued `static constexpr` data members in the
  MSVC STL numeric-limits hierarchy.
- **Impact**: Windows/MSVC `<limits>` analysis is no longer "time to first UCRT
  parse failure", but the header still does not complete semantic analysis.

---

## 3) Pointer-NTTP full specialization dispatch: second lookup produces wrong arg type

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

---

## 5) `tests/std/test_std_ratio.cpp` — no `.o` output and residual diagnostic noise

**Status**: Crash (SIGSEGV/exit 139) fixed. Two root-cause bugs were fixed:

1. **Infinite mutual recursion** between `materializeStoredTemplateArgs` and
   `substituteQualifiedIdentifier` (cycle guards added in `ExpressionSubstitutor`).
2. **Explicit template type args ignored** for zero-param function template calls in
   the constexpr evaluator (`ConstExprEvaluator_Core.cpp`).

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 5a) `std::__is_complete_or_unbounded` instantiation always fails

- **Symptom**: Many `[ERROR][Templates] All 2 template overload(s) failed for '__is_complete_or_unbounded'`
  during parsing of `<ratio>` and its dependencies.
- **Root cause**: `constexpr true_type __is_complete_or_unbounded(__type_identity<_Tp>)` is a function
  template with a non-type default template parameter (`size_t = sizeof(_Tp)`). FlashCpp does not
  yet support template argument deduction from a class-template-specialization argument
  (`__type_identity<_Tp>` → deduce `_Tp`) combined with a defaulted non-type parameter that
  depends on the deduced type.
- **Affected path**: `Parser_Templates_Inst_Deduction.cpp` — `tryInstantiateTemplateFromCallArguments`.
- **Impact**: Non-fatal during parsing (expressions remain dependent), but prevents correct
  constexpr evaluation of `static_assert` checks inside `<type_traits>` and `<ratio>`.

### 5b) `std::__are_both_ratios` parse-time instantiation failure (expected noise)

- **Symptom**: `[WARN][Parser] Parsed template arguments but instantiation failed for 'std::__are_both_ratios'`
  followed by `[ERROR][Templates] All 1 template overload(s) failed for '__are_both_ratios'` — appears
  4 times during `__ratio_multiply`/`ratio_equal` template body parsing.
- **Root cause**: The parser speculatively tries to instantiate `__are_both_ratios<_R1, _R2>` at
  template-body parse time, when `_R1` and `_R2` are still dependent type parameters.
  `try_instantiate_template_explicit` has an early-out for dependent args (correct behaviour).
  The errors are non-fatal and the constexpr evaluator path correctly handles them via
  the explicit-type-args fix (Bug 2 above), but `__is_complete_or_unbounded` failure (5a above)
  prevents full `static_assert` evaluation.
- **Impact**: Diagnostic noise only; not a correctness failure in isolation.

### 5c) Residual substitution cycle in `ExpressionSubstitutor` for partially-dependent `ratio` instances

- **Symptom**: Repeated DEBUG log lines cycling between `_R1::num` and `__static_abs$xxx::value`
  lookups during depth=2 `ratio` template processing.
- **Root cause**: When `__ratio_multiply<ratio<N1,D1>, ratio<N2,D2>>::type` is computed, intermediate
  partially-substituted `ratio<expr, expr>` instances store their `num`/`den` as still-dependent
  expressions pointing to `__static_abs` helper types. Those helpers in turn store the original
  `_R1::num`/`_R2::num` as their template arg. The two cycle guards (keyed on TypeInfo* and
  qualified-id key) detect each individual back-edge but the two-node cycle
  (`"_R1::num"` ↔ `"__static_abs$xxx::value"`) uses distinct keys, so substitution terminates
  by returning raw/unresolved args rather than resolved numeric values.
- **Affected path**: `ExpressionSubstitutor::substituteQualifiedIdentifier` /
  `materializeStoredTemplateArgs` for intermediate instantiation contexts.
- **Impact**: `ratio<N,D>::num` and `::den` remain as unresolved dependent expressions in
  `__ratio_multiply` results; `ratio_equal`, `ratio_less`, etc. cannot be evaluated.
  This prevents `.o` output for `tests/std/test_std_ratio.cpp` (codegen requires resolved types).
- **Fix direction**: Track the full two-node cycle by keying both nodes together, or resolve
  numeric NTTP values eagerly when concrete integer template args are available.
