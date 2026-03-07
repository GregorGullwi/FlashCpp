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

### 1.2 Free-Function Operator Overloads Invisible to Binary Operator Resolution ❌

**Standard (C++20 [over.match.oper]):** Resolving `a op b` must consider both member and
non-member (free-function) operator overloads from ordinary lookup and ADL.

**FlashCpp:** `findBinaryOperatorOverload` (OverloadResolution.h:734) only searches member
functions of the left operand's class and its bases. Free-function operators declared at
namespace scope are silently ignored.

```
// TODO: In the future, also search for free function operator overloads
```

**Location:** `src/OverloadResolution.h:734`

---

### 1.3 Overload Resolution Falls Back to First Overload When No Match Found ❌

**Standard (C++20 [over.match]):** When multiple overloads are viable but none is uniquely
best the call is ill-formed (ambiguous) and must produce a diagnostic. When no overload is
viable the call is also ill-formed.

**FlashCpp:** After exact-match and count-match phases both fail, `lookup_function`
(SymbolTable.h:585) silently returns the *first* registered overload:

```cpp
// Fallback: return the first overload
return overloads[0];
```

Ambiguous and no-viable-overload cases are accepted without a diagnostic; the program may
link and produce wrong results.

**Location:** `src/SymbolTable.h:585`

---

### 1.5 Name Mangling Uses Wrong Fallback Types for Unknown Kinds ❌

**Standard / ABI:** Both Itanium ABI and MSVC mangling are deterministic. An unknown or
unrecognised type has no valid fallback encoding; the correct behaviour is a compile error.

**FlashCpp:**
- `appendItaniumTypeCode` (NameMangling.h:327) falls back to `'i'` (int) for unknown scalar
  types and `'v'` (void) for unknown struct types.
- `appendMsvcTypeCode` (NameMangling.h:183, 187) falls back to `'H'` (int).

The resulting symbol names do not match those produced by GCC/Clang/MSVC; linking with
external libraries or system code that uses these types will fail silently.

**Location:** `src/NameMangling.h:183, 187, 321, 327, 562, 566`
