# FlashCpp Non-Standard Behavior — Semantic Analysis

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/SymbolTable.h`, `src/OverloadResolution.h`, `src/NameMangling.h`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.1 Argument-Dependent Lookup (ADL) ✅

**Standard (C++20 [basic.lookup.argdep]):** For an unqualified call `f(args…)` whose arguments
have class or enumeration type, lookup must also search the *associated namespaces* of the
argument types (Koenig lookup).

**FlashCpp:** ADL is implemented via `SymbolTable::lookup_adl()` (full ADL: regular namespace-
scope functions + hidden friends) and `SymbolTable::lookup_adl_only()` (hidden friends only).
Both struct/class and enum argument types are handled:
- **Structs:** associated namespaces include the declaring namespace plus namespaces of all base
  classes (recursive).
- **Enums:** the associated namespace is the innermost enclosing namespace, resolved via
  `TypeInfo::namespaceHandle()`. Enums nested inside class bodies are correctly registered under
  their struct-qualified name (e.g., `ns::Container::Status`) so that type resolution finds the
  correct `TypeInfo` with the proper namespace handle.
- **Hidden friends:** functions declared with `friend` inside a class body are stored in
  `adl_only_symbols_` via `insert_into_namespace(..., adl_only=true)`, making them invisible to
  ordinary unqualified lookup but reachable via ADL.

**Location:** `src/SymbolTable.h` — `lookup_adl()`, `lookup_adl_only()`, `insert_into_namespace()`,
`collect_struct_associated_namespaces()`; `src/Parser_Decl_StructEnum.cpp` — nested enum
qualified-name registration.

---

### 1.2 Binary Operator Overload Resolution with ADL ✅

**Standard (C++20 [over.match.oper]):** Resolving `a op b` must consider both member and
non-member (free-function) operator overloads from ordinary lookup and ADL, ranking all
candidates together via standard overload resolution. Per [over.match.oper]/3.3, when a member
and non-member candidate have identical conversion ranks, the member is preferred.

**FlashCpp:** `findBinaryOperatorOverloadWithFreeFunction` (OverloadResolution.h) collects
member-function candidates (recursive through base classes) and free-function candidates from
both `lookup_all()` (scope-chain) and `lookup_adl()` (associated namespaces, including hidden
friends) into a single unified candidate set. All candidates are ranked together using
`rankBinaryOperatorOperandMatch()` with the same `ConversionRank`-based comparison. ADL results
are deduplicated against scope-chain results using mangled names to avoid false ambiguity.
Member-preference tiebreaking is applied per [over.match.oper]/3.3.

**Location:** `src/OverloadResolution.h` — `findBinaryOperatorOverloadWithFreeFunction()`,
`OperatorOverloadResult::is_free_function`; `src/IrGenerator_Expr_Operators.cpp` — free-function
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
- `Type::Nullptr` — `Dn` (Itanium) / `$$T` (MSVC)
- `Type::Auto` — throws `InternalError` (should never reach mangling; indicates a compiler bug)

**Location:** `src/NameMangling.h` — `appendItaniumTypeCode()`, `appendMsvcTypeCode()`
