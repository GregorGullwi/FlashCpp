# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Range-for with inline struct iterator member functions

Range-for loops using struct iterators with inline member function definitions
(operator*, operator++, operator!=) crash at runtime (signal 11). Out-of-line
definitions work correctly. See `tests/test_range_for_auto_struct_iterator_ret0.cpp`
for a working pattern.

## Enum constant value category in sema-owned constructor argument typing

**Severity**: high — causes wrong constructor selection
**Regression test**: `tests/test_ctor_enum_prvalue_ret0.cpp`
**Tracking**: `docs/2026-04-04-codegen-name-lookup-investigation.md` Phase 2, item 6

`inferExpressionValueCategory` in `src/SemanticAnalysis.cpp` must classify
enum constants (enumerators) as `PRValue`, not `LValue`. The current
`IdentifierBinding::EnumConstant` guard is unreliable: when sema marks an
enumerator as an lvalue, `getExpressionType` encodes `ReferenceQualifier::LValueReference`
on the returned type, and constructor overload resolution prefers a
`const T&` overload over a `T&&` overload — even when the `const T&`
overload is deleted.

Reproducer:

```cpp
// Test: enum constants stay prvalues in sema-owned constructor argument typing.
// If sema marks the enumerator as an lvalue, overload resolution picks the
// deleted const-reference overload instead of the rvalue-reference overload.
enum Number {
	Seven = 7
};
struct Sink {
	int value;
	Sink(const Number&) = delete;
	Sink(Number&& number)
		: value((int)number) {}
};
int main() {
	Sink sink(Seven);
	return sink.value - 7;
}
```

Expected return value: `0`
Actual return value: `92`
