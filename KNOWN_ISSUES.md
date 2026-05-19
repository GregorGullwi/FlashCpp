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

## 3) Out-of-line template member signature validation is too literal

- **Symptom**: Parser warnings like `Parameter 1 type mismatch in out-of-line template member ...` remain for
  valid declarations/definitions that differ only by dependent spelling/aliases.
- **Root cause**: `validate_signature_match` compares low-level type fields (`type_index`, category, pointer/ref)
  too rigidly for dependent contexts and does not use a more canonicalized/dependent-aware type equivalence.
- **Affected path**: `Parser::validate_signature_match`, invoked from out-of-line template member parsing.
- **Impact**: Noisy diagnostics and increased risk of picking/validating against suboptimal overload candidates.

---

## 4) `tests/std/test_std_ratio.cpp` — no `.o` output and residual diagnostic noise

**Status**: Crash (SIGSEGV/exit 139) fixed. Two root-cause bugs were fixed:

1. **Infinite mutual recursion** between `materializeStoredTemplateArgs` and
   `substituteQualifiedIdentifier` (cycle guards added in `ExpressionSubstitutor`).
2. **Explicit template type args ignored** for zero-param function template calls in
   the constexpr evaluator (`ConstExprEvaluator_Core.cpp`).

**Current frontier**: The previous `__ratio_less_impl` / remove-cv alias
instantiation stop is fixed. The first hard blocker has moved later to
`__ratio_add_impl` default-NTTP evaluation, currently surfacing as unresolved
`ratio_less$...::value` during constant evaluation.

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 4a) `std::__are_both_ratios` parse-time instantiation failure (expected noise)

- **Symptom**: `[WARN][Parser] Parsed template arguments but instantiation failed for 'std::__are_both_ratios'`
  followed by `[ERROR][Templates] All 1 template overload(s) failed for '__are_both_ratios'` — appears
  4 times during `__ratio_multiply`/`ratio_equal` template body parsing.
- **Root cause**: The parser speculatively tries to instantiate `__are_both_ratios<_R1, _R2>` at
  template-body parse time, when `_R1` and `_R2` are still dependent type parameters.
  `try_instantiate_template_explicit` has an early-out for dependent args (correct behaviour).
  The errors are non-fatal and the constexpr evaluator path correctly handles
  them via the explicit-type-args fix above; the current hard stop is later
  default-NTTP/value propagation in ratio arithmetic.
- **Impact**: Diagnostic noise only; not a correctness failure in isolation.

### 4b) Residual substitution cycle/default-NTTP value loss in partially-dependent `ratio` instances

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
