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

## 2) `tests/std/test_std_ratio.cpp` — now reaches link-time unresolved CRT symbols

**Status**: Crash (SIGSEGV/exit 139) fixed. Two root-cause bugs were fixed:

1. **Infinite mutual recursion** between `materializeStoredTemplateArgs` and
   `substituteQualifiedIdentifier` (cycle guards added in `ExpressionSubstitutor`).
2. **Explicit template type args ignored** for zero-param function template calls in
   the constexpr evaluator (`ConstExprEvaluator_Core.cpp`).

**Current frontier**: The previous `__ratio_less_impl` / remove-cv alias
instantiation stop, default-NTTP declared-type materialization issue, MSVC
`type_traits` dependent member-template argument parse failure, deferred-base
call-expression invariant, `std::ratio_less<third, half>` constexpr
comparison failure, and the later pointer/null IR-conversion failures in
`wcsnlen_s` / `strnlen_s` are fixed. On current `main`, `test_std_ratio.cpp`
now compiles through IR generation and instead stops at link time on unresolved
CRT helper symbols.

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 2a) Standard-library template-instantiation noise during `<ratio>` compile

- **Symptom**: compiling `tests/std/test_std_ratio.cpp` now emits
  `[ERROR][Templates] [depth=1]: All 2 template overload(s) failed for 'swap'`,
  the same error for `std::swap`, and
  `[ERROR][Templates] [depth=1]: All 1 template overload(s) failed for '_Hash_representation'`
  before the later IR-conversion failure.
- **Root cause**: still under investigation. These diagnostics appear during
  standard-header template processing after the earlier dependent member-template
  parse failure was fixed, but they are not the first fatal stop in the
  compilation.
- **Impact**: Diagnostic noise only; not a correctness failure in isolation.

### 2b) Link-time unresolved CRT/UCRT helper symbols

- **Symptom**: `tests/std/test_std_ratio.cpp` now compiles successfully but
  fails to link with unresolved externals such as `wcstok`, `wcspbrk`,
  `strnlen`, and `strpbrk`.
- **Root cause**: Still under investigation. The remaining blocker has moved
  beyond sema/IR conversion and now appears to be missing runtime or import
  coverage for CRT/UCRT helpers referenced by the instantiated standard-library
  surface.
- **Affected path**: Link stage for the object generated from `<ratio>` and the
  headers it pulls in.
- **Impact**: `tests/std/test_std_ratio.cpp` still does not produce a runnable
  executable, even though it now compiles.
- **Fix direction**: Audit the unresolved functions to determine whether
  FlashCpp should emit/import them differently or whether the Windows link step
  is currently missing required CRT coverage for these overloads/vararg forms.
