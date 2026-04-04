# FlashCpp Non-Standard Behavior — Preprocessor & Macros

[← Index](../NON_STANDARD_BEHAVIOR.md)

Most items in this section are implemented in `src/FileReader_Macros.cpp`; direct `#pragma once`
handling also touches `src/FileReader_Core.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

Audit status (2026-03-07): 7.1, 7.3, 7.4, and 7.5 were updated after implementation work; 7.2
was rechecked against the current implementation and remains accurate.

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

**Status update (2026-03-07):** `_Pragma("once")` support implemented and covered by
`tests/test_pragma_once_operator_ret42.cpp`.

**Location:** `src/FileReader_Macros.cpp:171–180`, `src/FileReader_Core.cpp:427–428`

---

### 7.2 `__GNUC__` and `_MSC_VER` Both Always Defined ❌

**Standard:** A compiler should identify as one vendor. Defining both `__GNUC__` (GCC/Clang)
and `_MSC_VER` (MSVC) simultaneously is non-standard.

**FlashCpp:** Both macros are always defined regardless of target mode:
- `__GNUC__ = "12"` (FileReader_Macros.cpp:1377) — for `libstdc++` compatibility.
- `_MSC_VER = "1944"` (FileReader_Macros.cpp:1463) — for MSVC header compatibility.

Headers that use `#if defined(_MSC_VER) && !defined(__GNUC__)` or similar exclusive guards
take unexpected paths.

**Location:** `src/FileReader_Macros.cpp:1377, 1463`

---

### 7.3 `constexpr` Feature-Test Macros Partially Corrected ⚠️

**Standard (SD-6):** `__cpp_*` macros shall only be defined if the corresponding feature is
fully implemented.

**Status update (2026-03-07):**
- `__cpp_consteval` is no longer defined.
- `__cpp_constexpr_dynamic_alloc` is no longer defined.
- `__cpp_constexpr` has been reduced from `202002L` to `201603L`.

This avoids advertising full C++20 `consteval` / constexpr-dynamic-allocation support to system
headers, but FlashCpp's constexpr evaluator still has documented gaps beyond what real-world code
may expect from a mature C++17/C++20 compiler.

**Previously over-advertised macros:**

| Macro | Value | Gap |
|-------|-------|-----|
| `__cpp_consteval` | *(now undefined)* | Kept undefined conservatively to avoid over-advertising feature completeness to system headers |
| `__cpp_constexpr` | `201603L` | Some constexpr evaluator gaps remain; see `docs/non_standard/05_constexpr.md` and `docs/CONSTEXPR_LIMITATIONS.md` |
| `__cpp_constexpr_dynamic_alloc` | *(now undefined)* | `constexpr std::string` / `std::vector` and dynamic allocation in constexpr are not implemented |

**Regression coverage:** `tests/test_constexpr_feature_macros_ret0.cpp`

**Location:** `src/FileReader_Macros.cpp:1497–1514`

---

### 7.4 `__asm` / `__asm__` Parsed as Suffixes; Function Symbol Rename Preserved ⚠️

**Context:** `asm` declarations (`extern T f() __asm("impl_name")`) appear in system headers
for symbol renaming.

**Status update (2026-03-07):**
- `__asm("...")` and `__asm__("...")` are no longer preprocessor macros.
- The parser now accepts them as ignorable declaration suffixes on variables and functions.
- Function declarations now preserve the requested external symbol name instead of always
  discarding it.

This fixes the keyword-vs-macro mismatch and makes `#ifdef __asm` / `#ifdef __asm__` evaluate
to false, matching GCC-style keyword behavior more closely.

**Remaining gap:** Variable declarations still discard the requested assembler symbol rename.
`extern int value __asm("value_impl");` still behaves as though the rename were absent, so
references continue to use `value` rather than `value_impl`.

**Regression coverage:** `tests/test_asm_function_rename_ret42.cpp`

**Location:** parser handling in `src/Parser_Expr_BinaryPrecedence.cpp` and
`src/Parser_Decl_DeclaratorCore.cpp`

---

### 7.5 `__restrict` Parsed as a No-Op Keyword ✅

**Context:** `__restrict` is a GCC/Clang extension that appears pervasively in system headers.
The correct handling is to treat it as a no-op keyword (not a macro).

**Status update (2026-03-07):**
- `__restrict` is no longer defined as a preprocessor macro.
- The parser now treats `__restrict` as an ignorable qualifier in declarators.

This keeps common header declarations working while making `#ifdef __restrict` evaluate to
*false*, matching GCC/Clang behavior much more closely.

**Regression coverage:** `tests/test_restrict_keyword_ret42.cpp`

**Location:** parser handling in `src/Parser_Expr_BinaryPrecedence.cpp` and
`src/Parser_Decl_DeclaratorCore.cpp`

---

### 6.1 `__cpp_exceptions` ✅ Defined

**Standard (SD-6):** `__cpp_exceptions` must be defined when exception handling is supported.
Standard library headers (`<stdexcept>`, `<new>`, etc.) guard their exception-throwing paths
with `#if __cpp_exceptions`.

**FlashCpp:** `__cpp_exceptions 199711L` is defined in `FileReader_Macros.cpp`. Full
`throw`/`catch` with primitives and class types works on both Linux and Windows.

**Location:** `src/FileReader_Macros.cpp:1515`
