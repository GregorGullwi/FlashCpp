# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) `std::_Optional_payload` / `std::_Tuple_impl` — type size 0 in IR codegen (OPEN)

- **Symptom**: `IR conversion failed for node 'move': Type with no runtime size reached
  codegen in reference identifier lvalue lowering (type=28, ...)` when compiling
  `<optional>` and `<tuple>`.
- **Root cause**: `type=28` is a struct/class type whose size was not computed before
  codegen. The payload/impl types are recursive union variants whose sizeof is not
  resolved during template instantiation.
- **Impact**: `test_std_optional`, `test_std_tuple` fail.

---

## 2) Standard-library template-instantiation noise during `<ratio>` compile

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

## 3) Windows/MSVC standard-header link step emits duplicate CRT/runtime symbols (OPEN)

- **Symptom**: after the `<limits>` compile-stage fix, manually linking the object
  produced from `tests/std/test_std_limits.cpp` can fail with multiple-definition
  errors such as `__security_cookie`, `__local_stdio_printf_options`,
  `__local_stdio_scanf_options`, `vsnprintf`, `_vsprintf_s_l`, `sprintf_s`, and
  `snprintf`.
- **Root cause**: still under investigation. FlashCpp's generated object appears
  to emit CRT/runtime definitions that should remain owned by the MSVC CRT when
  standard headers pull in the corresponding declarations and wrappers.
- **Impact**: some Windows/MSVC standard-header tests now compile through object
  generation but still do not link cleanly outside the main regression suite.

---

## 4) OOL member-function-template dependent member-template pointer parameter dereference (OPEN)

- **Symptom**: A reduced out-of-line member-function-template body using
  `typename T::template AddPtr<int>::type` as a parameter can still crash at
  runtime when the body directly dereferences or forwards that parameter as an
  `int*`, even though signature replay now accepts the member-template type
  chain.
- **Root cause**: The replay/signature path can resolve the chain for matching,
  but some lazy member-function-template body/codegen surfaces still preserve
  placeholder ABI metadata for the parameter.
- **Impact**: Regression coverage currently checks pointer substitution through
  `sizeof(value)` rather than direct dereference until the remaining body/codegen
  metadata gap is closed.
