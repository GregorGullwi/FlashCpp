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

## 2) `std::_Optional_payload` / `std::_Tuple_impl` — type size 0 in IR codegen (OPEN)

- **Symptom**: `IR conversion failed for node 'move': Type with no runtime size reached
  codegen in reference identifier lvalue lowering (type=28, ...)` when compiling
  `<optional>` and `<tuple>`.
- **Root cause**: `type=28` is a struct/class type whose size was not computed before
  codegen.  The payload/impl types are recursive union variants whose sizeof is not
  resolved during template instantiation.
- **Impact**: `test_std_optional`, `test_std_tuple` fail.

---

## 3) `tests/std/test_std_ratio.cpp` — link-time `__security_cookie` conflict (OPEN)

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 4a) Standard-library template-instantiation noise during `<ratio>` compile

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

### 4b) Link-time `__security_cookie` multiple-definition conflict

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
