# FlashCpp Non-Standard Behavior — Constant Expressions (`constexpr` / `consteval`)

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/ConstExprEvaluator_Core.cpp`, `src/ConstExprEvaluator_Members.cpp`.
See also `docs/CONSTEXPR_LIMITATIONS.md` for a more detailed treatment of items marked **[Known]**.

**Scope note:** this document describes FlashCpp behavior against **C++20 constexpr**.
It does **not** claim support for constexpr exception handling. Any mention of
internal `catch` behavior below refers to evaluator implementation/diagnostics,
not to `throw` / `try` / `catch` being supported during constant evaluation.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 4.4 `sizeof(arr)` May Be Parsed as `sizeof(type)` in Some Contexts ⚠️

**Standard:** `sizeof(arr)` where `arr` is an array variable must be treated as
`sizeof(expression)`, yielding the full array size.

**FlashCpp:** In some paths the parser incorrectly treats `arr` as a type name. Two
workarounds exist that detect `size_in_bits == 0` and fall back to a symbol table lookup:

**Location:** `src/ConstExprEvaluator_Core.cpp:933–976`, `src/IrGenerator_MemberAccess.cpp:1368–1500`

---

### 4.5 Catch-All Returns Runtime Error Instead of a Compile-Time Error ⚠️

**Standard:** If a `constexpr` expression cannot be evaluated at compile time for a required
constant-expression context, the program is ill-formed and the compiler must diagnose it.

**FlashCpp:** `ConstExprEvaluator_Core.cpp:508` has an internal catch-all that returns
`EvalResult::error(…)` at *runtime*, which causes the caller to silently treat the expression
as non-constant and fall back to a codegen path that may produce wrong code without any
visible diagnostic.

This is a diagnostics/implementation issue, **not** support for constexpr
exception handling.

**Location:** `src/ConstExprEvaluator_Core.cpp:508`

---

### 2.3 `[this]` and `[*this]` Constexpr Lambda Capture Is Partial ⚠️

**Standard (C++20 [expr.const]):** A `constexpr` lambda in a `constexpr` member function may
capture `this` or `*this`.

**FlashCpp:** Basic supported shapes now work, including simple member reads/calls through
explicit or implicit `this` capture, straightforward mutable shared-object updates through
`[this]`, and straightforward mutable `[*this]` copy-local updates. Richer captured-object
aliasing/identity behavior still remains partial.

**Location:** `src/ConstExprEvaluator_Core.cpp`, `src/ConstExprEvaluator_Members.cpp`, `docs/CONSTEXPR_LIMITATIONS.md:254–282`

---

### 3.3 `std::integral_constant::value` Synthesised From Template Arguments, Bypassing Lookup ⚠️

**Standard:** `std::integral_constant<T,v>::value` is a well-defined static `constexpr` data
member accessed through normal name lookup and constexpr evaluation.

**FlashCpp:** `ConstExprEvaluator_Members.cpp:903` synthesises the value directly from the
template argument when the static member is not registered, labelled as:

> *"Fallback: synthesize integral_constant::value from template arguments when static member
> isn't registered"*

This gives the correct result for the primary template but would give wrong answers for any
explicit specialisation of `integral_constant`.

**Location:** `src/ConstExprEvaluator_Members.cpp:903`
