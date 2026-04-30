# FlashCpp Non-Standard Behavior — Preprocessor & Macros

[← Index](../NON_STANDARD_BEHAVIOR.md)

Most items in this section are implemented in `src/FileReader_Macros.cpp`; direct `#pragma once`
handling also touches `src/FileReader_Core.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 7.1 `_Pragma()` Only Processes `once` / `pack`; All Other Pragmas Silently Discarded ⚠️

**Standard (C++20 [cpp.pragma.op]):** `_Pragma(string-literal)` destringises its argument
and processes it as a `#pragma` directive.

**FlashCpp:** `_Pragma("once")` and `_Pragma("pack(...)")` are processed. Other pragmas such
as `_Pragma("comment")`, `_Pragma("warning")`, `_Pragma("GCC ...")`, and
`_Pragma("clang ...")` are silently discarded.

Direct `#pragma once` support already existed in `src/FileReader_Core.cpp`; `_Pragma("once")`
now feeds the same `processedHeaders_` mechanism, so third-party headers using `_Pragma("once")`
are guarded correctly.

**Location:** `src/FileReader_Macros.cpp:171–180`, `src/FileReader_Core.cpp:427–428`

---

### 7.2 `__GNUC__` and `_MSC_VER` Both Always Defined ❌

**Standard:** A compiler should identify as one vendor. Defining both `__GNUC__` (GCC/Clang)
and `_MSC_VER` (MSVC) simultaneously is non-standard.

**FlashCpp:** Both macros are always defined regardless of target mode:
- `__GNUC__ = "12"` (FileReader_Macros.cpp:1389) — for `libstdc++` compatibility.
- `_MSC_VER = "1944"` (FileReader_Macros.cpp:1469) — for MSVC header compatibility.

Headers that use `#if defined(_MSC_VER) && !defined(__GNUC__)` or similar exclusive guards
take unexpected paths.

**Location:** `src/FileReader_Macros.cpp:1389, 1469`

---

### 7.3 `constexpr` Feature-Test Macros Partially Corrected ⚠️

**Standard (SD-6):** `__cpp_*` macros shall only be defined if the corresponding feature is
fully implemented.

- `__cpp_constexpr_dynamic_alloc` is not defined.
- `__cpp_constexpr` is reduced to `201603L`.
- But `__cpp_consteval` is currently still defined as `201811L` even though FlashCpp's
  constexpr/consteval support remains incomplete.

So the macro surface is only partially conservative: some over-advertising was removed, but
`__cpp_consteval` still claims support that the implementation does not fully provide.

| Macro | Value | Gap |
|-------|-------|-----|
| `__cpp_consteval` | `201811L` | Advertises consteval support more strongly than the implementation justifies |
| `__cpp_constexpr` | `201603L` | Some constexpr evaluator gaps remain; see `docs/non_standard/05_constexpr.md` and `docs/CONSTEXPR_LIMITATIONS.md` |
| `__cpp_constexpr_dynamic_alloc` | *(now undefined)* | `constexpr std::string` / `std::vector` and dynamic allocation in constexpr are not implemented |

**Regression coverage:** `tests/test_feature_macros_ret0.cpp`

**Location:** `src/FileReader_Macros.cpp:1549–1550`
