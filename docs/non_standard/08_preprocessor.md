# FlashCpp Non-Standard Behavior — Preprocessor & Macros

[← Index](../NON_STANDARD_BEHAVIOR.md)

Most items in this section are implemented in `src/FileReader_Macros.cpp`; direct `#pragma once`
handling also touches `src/FileReader_Core.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

Audit status (2026-03-07): 7.1 and 7.3 were updated after implementation work; 7.2, 7.4, and
7.5 were rechecked against the current implementation and remain accurate.

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
| `__cpp_consteval` | *(now undefined)* | Compile-time-only enforcement not implemented (§4.3) |
| `__cpp_constexpr` | `201603L` | Some constexpr evaluator gaps remain; see `docs/non_standard/05_constexpr.md` and `docs/CONSTEXPR_LIMITATIONS.md` |
| `__cpp_constexpr_dynamic_alloc` | *(now undefined)* | `constexpr std::string` / `std::vector` and dynamic allocation in constexpr are not implemented |

**Regression coverage:** `tests/test_constexpr_feature_macros_ret0.cpp`

**Location:** `src/FileReader_Macros.cpp:1497–1514`

---

### 7.4 `__asm` / `__asm__` Stripped to Empty String ⚠️

**Context:** `asm` declarations (`extern T f() __asm("impl_name")`) appear in system headers
for symbol renaming.

**FlashCpp:** Both `__asm` and `__asm__` are defined as function-like macros that expand to
the empty string (FileReader_Macros.cpp:1424–1425). Any `extern T f() __asm("f_impl")`
declaration will have its rename silently dropped; `f()` links as `f` rather than `f_impl`,
producing undefined-symbol linker errors when the system library exposes `f_impl`.

**Location:** `src/FileReader_Macros.cpp:1424–1425`

---

### 7.5 `__restrict` Defined as an Empty Macro ⚠️

**Context:** `__restrict` is a GCC/Clang extension that appears pervasively in system headers.
The correct handling is to treat it as a no-op keyword (not a macro).

**FlashCpp:** `defines_["__restrict"] = DefineDirective{};` expands `__restrict` to the empty
string. This works for the common case but means `#ifdef __restrict` evaluates to *true*
(defined, but as an empty macro), which differs from GCC/Clang where `__restrict` is a keyword,
not a macro, so `#ifdef __restrict` would be *false*.

**Location:** `src/FileReader_Macros.cpp:1381`

---

### 6.1 `__cpp_exceptions` Not Defined; Standard Library Uses Abort Paths ⚠️

**Standard (SD-6):** `__cpp_exceptions` must be defined when exception handling is supported.
Standard library headers (`<stdexcept>`, `<new>`, etc.) guard their exception-throwing paths
with `#if __cpp_exceptions`.

**FlashCpp:** The macro is intentionally absent (FileReader_Macros.cpp:1518–1520). Even the
partial exception handling that does work (simple `throw`/`catch` of primitives on Linux and
Windows) is therefore invisible to the standard library. `std::bad_alloc`, `std::out_of_range`,
and similar types will call `std::abort()` rather than throw.

**Location:** `src/FileReader_Macros.cpp:1518–1520`
