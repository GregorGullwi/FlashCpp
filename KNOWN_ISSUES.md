# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## KI-002 · Implicit constructor / assignment-operator bodies emitted for simple aggregates

**Severity:** QoI (unnecessary code size increase; no correctness impact).

**Status:** Open

### Symptom

For a plain aggregate struct with no user-declared constructors or operators:

```cpp
struct Point { int x; int y; };
```

FlashCpp emits full function bodies for the default constructor, copy constructor,
move constructor, copy assignment operator, and move assignment operator
(confirmed in disassembly of `/tmp/probe.o`). GCC and Clang elide these entirely when the
type is trivially copyable and the implicitly-defined functions are never ODR-used.

### Root Cause

The implicit-member generation pass in the IR generator emits codegen bodies for all
implicitly-declared special members unconditionally, rather than checking whether they are
ODR-used before emitting.

### Recommended Fix

Track ODR-use of implicit special members and defer emission until a use is observed.
Trivially copyable types whose special members are never explicitly called (e.g., a
`constexpr` global struct that is only read by field) should produce zero emitted bodies.

---
