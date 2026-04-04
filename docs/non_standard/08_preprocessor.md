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

- `__cpp_consteval` is no longer defined.
- `__cpp_constexpr_dynamic_alloc` is no longer defined.
- `__cpp_constexpr` has been reduced from `202002L` to `201603L`.

This avoids advertising full C++20 `consteval` / constexpr-dynamic-allocation support to system
headers, but FlashCpp's constexpr evaluator still has documented gaps beyond what real-world code
may expect from a mature C++17/C++20 compiler.

| Macro | Value | Gap |
|-------|-------|-----|
| `__cpp_consteval` | *(now undefined)* | Kept undefined conservatively to avoid over-advertising feature completeness to system headers |
| `__cpp_constexpr` | `201603L` | Some constexpr evaluator gaps remain; see `docs/non_standard/05_constexpr.md` and `docs/CONSTEXPR_LIMITATIONS.md` |
| `__cpp_constexpr_dynamic_alloc` | *(now undefined)* | `constexpr std::string` / `std::vector` and dynamic allocation in constexpr are not implemented |

**Regression coverage:** `tests/test_constexpr_feature_macros_ret0.cpp`

**Location:** `src/FileReader_Macros.cpp:1513`

---

### 7.4 `__asm` / `__asm__` Parsed as Suffixes; Function and Variable Symbol Rename Preserved ✅

**Context:** `asm` declarations (`extern T f() __asm("impl_name")`) appear in system headers
for symbol renaming.

- `__asm("...")` and `__asm__("...")` are no longer preprocessor macros.
- The parser now accepts them as ignorable declaration suffixes on variables and functions.
- Function and variable declarations now preserve the requested external symbol name instead of
  discarding it.

This fixes the keyword-vs-macro mismatch and makes `#ifdef __asm` / `#ifdef __asm__` evaluate
to false, matching GCC-style keyword behavior more closely.

**Regression coverage:** `tests/test_asm_function_rename_ret42.cpp`,
`tests/test_asm_variable_rename_ret42.cpp`

**Location:** parser handling in `src/Parser_Expr_BinaryPrecedence.cpp` and
`src/Parser_Decl_DeclaratorCore.cpp`
