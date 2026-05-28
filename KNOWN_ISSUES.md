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

## 3) OOL member-function-template overloads with dependent member-template alias parameters can fail attachment (OPEN)

- **Symptom**: Two out-of-line member-function-template overloads whose
  signatures differ only by `typename T::template AddPtr<int>::type` vs
  `typename T::template AddPtr<long>::type` can fail replay identity attachment;
  later calls may both select the `$ol0` materialization and link against a body
  that was never emitted.
- **Root cause**: The new alias-metadata path handles single-declaration body
  replay, but same-name overload preselection still does not fully distinguish
  member-template alias targets after owner substitution.
- **Impact**: Keep the next replay-metadata slice focused on overloaded
  member-template alias parameters once the non-overloaded forwarding paths stay
  stable.
