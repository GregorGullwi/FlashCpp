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

## 2) Missing type traits in compile-time evaluator (FIXED)

**Status**: Fixed in `src/ConstExprEvaluator_Members.cpp`.

`IsConstructible`, `IsTriviallyConstructible`, `IsNothrowConstructible`, `IsDestructible`,
`IsTriviallyDestructible`, `IsNothrowDestructible`, and `HasTrivialDestructor` all fell
through to `default: result = false` in the `evaluate_type_trait()` switch statement.
They now route through `evaluateTypeTrait()` from `TypeTraitEvaluator.h`, fixing
`static_assert(__is_nothrow_constructible(int))` and similar forms that were wrongly
evaluated as `false`.

---

## 3) Dependent function calls in template bodies not deferred (FIXED)

**Status**: Fixed in `src/Parser_Expr_PrimaryExpr.cpp`.

When parsing template member function bodies, calls to other template functions were
immediately instantiated (eager resolution). If the argument types were template-
dependent but `expressionHasDeferredTemplateDependency()` failed to detect that
(because `IdentifierNode` is not handled in the visitor), FlashCpp would emit a hard
"Failed to instantiate template function" error at definition time rather than deferring
to instantiation time as C++20 [temp.res] requires.

Fix: after all overload resolution fails, if `hasActiveTemplateParameters()` is true
(we're inside a template definition) and the callee is a known template in the registry,
create a deferred/dependent call expression instead of emitting an error.

**Remaining**: `expressionHasDeferredTemplateDependency` still does not handle
`IdentifierNode` — the fix is a safety net, not the ideal root-cause fix.

---

## 4) `std::_Optional_payload` / `std::_Tuple_impl` — type size 0 in IR codegen (OPEN)

- **Symptom**: `IR conversion failed for node 'move': Type with no runtime size reached
  codegen in reference identifier lvalue lowering (type=28, ...)` when compiling
  `<optional>` and `<tuple>`.
- **Root cause**: `type=28` is a struct/class type whose size was not computed before
  codegen.  The payload/impl types are recursive union variants whose sizeof is not
  resolved during template instantiation.
- **Impact**: `test_std_optional`, `test_std_tuple` fail.

---

## 5) `deallocate` IR: dereference of non-StringHandle/TempVar pointer (OPEN)

- **Symptom**: `IR conversion failed for node 'deallocate': Dereference pointer must be
  StringHandle or TempVar` when compiling `<deque>`, `<stack>`, and related headers.
- **Root cause**: The pointer operand arriving at the dereference lowering in
  `IrGenerator_Expr_Conversions.cpp:1793` carries a value variant that is neither
  `StringHandle` nor `TempVar` (likely an `int64_t` immediate from a null or constant
  pointer).
- **Impact**: `test_std_deque`, `test_std_stack` fail.

---

## 6) `__hash_enum` NTTP default evaluation failure (OPEN)

- **Symptom**: `error: Could not evaluate non-type template default for parameter 1 of
  '__hash_enum'` when compiling `<string>`, `<memory>`, `<stdexcept>`.
- **Root cause**: `__hash_enum`'s second non-type template parameter has a default
  `sizeof(_Tp) <= 8`. FlashCpp cannot evaluate this sizeof expression as an NTTP default
  at instantiation time.
- **Impact**: `test_std_string`, `test_std_memory`, `test_std_stdexcept` fail.

---

## 7) `lexicographical_compare_three_way` — non-dependent name lookup (OPEN)

- **Symptom**: `error: non-dependent name 'lexicographical_compare_three_way' was not
  declared before the template definition (C++20 [temp.res]/9)` in `<map>`, `<set>`.
- **Root cause**: FlashCpp enforces C++20 [temp.res]/9 strictly; the name is declared
  later in the same translation unit (through `<algorithm>`) but FlashCpp requires it
  to be visible at the point of the template definition.
- **Impact**: `test_std_map`, `test_std_set` fail.

---

## 8) `tests/std/test_std_ratio.cpp` — now reaches a link-time `__security_cookie` conflict

**Status**: Crash (SIGSEGV/exit 139) fixed. Two root-cause bugs were fixed:

1. **Infinite mutual recursion** between `materializeStoredTemplateArgs` and
   `substituteQualifiedIdentifier` (cycle guards added in `ExpressionSubstitutor`).
2. **Explicit template type args ignored** for zero-param function template calls in
   the constexpr evaluator (`ConstExprEvaluator_Core.cpp`).

**Current frontier**: The previous `__ratio_less_impl` / remove-cv alias
instantiation stop, default-NTTP declared-type materialization issue, MSVC
`type_traits` dependent member-template argument parse failure, deferred-base
call-expression invariant, `std::ratio_less<third, half>` constexpr
comparison failure, the later pointer/null IR-conversion failures in
`wcsnlen_s` / `strnlen_s`, and the link-time unresolved CRT helper symbols are
fixed. On current `main`, `test_std_ratio.cpp` now compiles and links further
before stopping on a multiply defined `__security_cookie`.

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

---

### 2b) Link-time `__security_cookie` multiple-definition conflict

- **Symptom**: `tests/std/test_std_ratio.cpp` now compiles successfully but
  fails to link with `LIBCMT.lib(gs_cookie.obj) : error LNK2005:
  __security_cookie already defined`.
- **Root cause**: Still under investigation. After fixing the imported-CRT
  naming issue (preserving `extern "C"` over `_ACRTIMP`/`dllimport` in parser
  linkage precedence), the next remaining blocker is that FlashCpp's output now
  collides with the CRT-provided GS cookie symbol.
- **Affected path**: Link stage for the object generated from `<ratio>` and the
  headers it pulls in.
- **Impact**: `tests/std/test_std_ratio.cpp` still does not produce a runnable
  executable, even though it now compiles.
- **Fix direction**: Audit where FlashCpp synthesizes or exports the GS cookie
  runtime state and make it coexist with the CRT-provided definition instead of
  emitting a second strong symbol.
