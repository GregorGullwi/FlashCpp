# FlashCpp Non-Standard Behavior — Semantic Analysis

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/SymbolTable.h`, `src/OverloadResolution.h`, `src/NameMangling.h`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.1 Argument-Dependent Lookup (ADL) ✅

**Standard (C++20 [basic.lookup.argdep]):** For an unqualified call `f(args…)` whose arguments
have class or enumeration type, lookup must also search the *associated namespaces* of the
argument types (Koenig lookup).

**FlashCpp:** ADL is implemented via `SymbolTable::lookup_adl()`. Both ordinary namespace-scope
functions and inline friend functions (hidden friends) are findable via ADL.

**Location:** `src/SymbolTable.h` — `lookup_adl()`, `insert_into_namespace()`

---

### 1.2 Free-Function Operator Overloads Invisible to Binary Operator Resolution ✅

**Standard (C++20 [over.match.oper]):** Resolving `a op b` must consider both member and
non-member (free-function) operator overloads from ordinary lookup and ADL.

**FlashCpp:** `findBinaryOperatorOverloadWithFreeFunction` (OverloadResolution.h) searches member
functions first, then falls back to namespace-scope free-function operators found in the symbol
table. Both member and non-member operator overloads are now fully supported.

**Location:** `src/OverloadResolution.h` — `findBinaryOperatorOverloadWithFreeFunction()`,
`OperatorOverloadResult::is_free_function`; `src/CodeGen_Expr_Operators.cpp` — free-function
codegen path with reference parameter handling.

---

### 1.3 Overload Resolution Falls Back to First Overload When No Match Found ✅

**Standard (C++20 [over.match]):** When multiple overloads are viable but none is uniquely
best the call is ill-formed (ambiguous) and must produce a diagnostic. When no overload is
viable the call is also ill-formed.

**FlashCpp:** `lookup_function` (SymbolTable.h) now returns `std::nullopt` when both exact-match
and count-match phases fail, instead of silently returning the first registered overload.

**Location:** `src/SymbolTable.h` — `lookup_function()`

---

### 1.5 Name Mangling Uses Wrong Fallback Types for Unknown Kinds ✅

**Standard / ABI:** Both Itanium ABI and MSVC mangling are deterministic. An unknown or
unrecognised type has no valid fallback encoding; the correct behaviour is a compile error.

**FlashCpp:** Both `appendItaniumTypeCode` and `appendMsvcTypeCode` now throw `CompileError` for
truly unknown types instead of silently emitting `'i'`/`'H'` (int). Previously-missing types
are now properly handled:
- `Type::Enum` — encoded by name (same as struct) from `gTypeInfo`
- `Type::FunctionPointer` — `PF<return><params>E` (Itanium) / `P6A<return><params>@Z` (MSVC)
- `Type::Nullptr` — `Dn` (Itanium) / `PAX` (MSVC)
- `Type::Auto` — throws `InternalError` (should never reach mangling; indicates a compiler bug)

**Location:** `src/NameMangling.h` — `appendItaniumTypeCode()`, `appendMsvcTypeCode()`
