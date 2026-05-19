# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) MSVC `<limits>` now stops later on `numeric_limits` constexpr member initialization

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

## 2) `tests/std/test_std_ratio.cpp` — no `.o` output and residual diagnostic noise

**Status**: Crash (SIGSEGV/exit 139) fixed. Two root-cause bugs were fixed:

1. **Infinite mutual recursion** between `materializeStoredTemplateArgs` and
   `substituteQualifiedIdentifier` (cycle guards added in `ExpressionSubstitutor`).
2. **Explicit template type args ignored** for zero-param function template calls in
   the constexpr evaluator (`ConstExprEvaluator_Core.cpp`).

**Current frontier**: The previous `__ratio_less_impl` / remove-cv alias
instantiation stop, default-NTTP declared-type materialization issue, MSVC
`type_traits` dependent member-template argument parse failure, deferred-base
call-expression invariant, and `std::ratio_less<third, half>` constexpr
comparison failure are fixed. The first hard blocker currently reaches IR
conversion for `test_std_ratio.cpp`.

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 2a) `std::__are_both_ratios` parse-time instantiation failure (expected noise)

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

### 2b) Residual ratio IR conversion failures

- **Symptom**: `tests/std/test_std_ratio.cpp` now parses the MSVC `type_traits`
  helpers and passes `static_assert(std::ratio_less<third, half>::value)`, then
  fails during IR conversion with missed scalar conversion diagnostics such as
  `wchar_t -> int`, `char -> int`, and `unsigned long long -> long long`.
- **Root cause**: Still under investigation. The remaining hard stop is now in
  IR conversion after the ratio comparison constexpr/value propagation path is
  evaluable.
- **Affected path**: IR conversion for standard-header helper functions and
  namespace-scope initializers reached while compiling `<ratio>`.
- **Impact**: `tests/std/test_std_ratio.cpp` still does not produce `.o` output.
- **Fix direction**: Minimize the new conversion diagnostics from the generated
  standard-header surface, then fix the missing sema-to-IR scalar conversion
  handoff without adding local `<ratio>` special cases.
