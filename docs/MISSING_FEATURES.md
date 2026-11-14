# Missing Features and C++20 Conformance

This document tracks missing C++20 features and implementation gaps in FlashCpp.

**Last Updated**: 2026-11-09 (re-verified against `arch/flat-consumer-gaps` @ e5e8f5c7)
**Overall C++20 Conformance Grade**: **A** for core language; standard-library compatibility is the main gap
- **Parser**: 97% complete
- **AST**: 97% complete
- **Code Generation**: 90% complete
- **Standard Library**: 21 of 71 bare `#include` probes compile on Windows/MSVC (2026-11-09); Linux/GCC not re-verified

## Summary

**Core C++20 Parsing**: Nearly complete! The parser has excellent coverage of C++20 features including concepts, requires expressions, templates, and modern syntax. Most missing features are in code generation edge cases and advanced language constructs.

**Status**: All critical parsing features for C++20 are implemented. Main remaining gaps are:
1. Coroutines (keywords recognized, parsing incomplete)
2. Modules (not implemented)
3. Some code generation edge cases
4. Standard-library compatibility (many MSVC STL headers now fail or crash; see Standard Library Support)

---

## C++20 Features Status

### Fully Implemented ✅

**Concepts and Constraints (A+)**
- Concept definitions: `concept Arithmetic = std::is_arithmetic_v<T>;`
- Requires clauses on templates: `template<typename T> requires Concept<T>`
- Trailing requires on functions: `void func() requires constraint { }`
- Requires expressions (all 4 requirement types):
  - Type requirements: `typename T::type;`
  - Simple requirements: `expression;`
  - Compound requirements: `{ expr } noexcept -> Concept;`
  - Nested requirements: `requires constraint;`
- Constraint composition: conjunctions and disjunctions
- Abbreviated function templates: `void func(Concept auto x)`
- Concept subsumption and ordering

**Template System (A)**
- Template parameters: type, non-type, template template parameters
- Parameter packs: `typename... Args`
- Non-type template parameters with `auto`: `template<auto V>`
- Template specializations: partial and full
- Fold expressions: `(args + ...)` and all variants
- Template template parameters: `template<typename> class Container`
- Variadic templates: All patterns including nested instantiation
- Perfect forwarding: `std::forward` preserves reference types
- SFINAE: Substitution failures handled gracefully
- Out-of-line template member definitions
- Out-of-line nested class member definitions: `template<T> class Foo<T>::Bar { ... }`

**Spaceship Operator `<=>` (A+)**
- Parsing: Complete
- Defaulted `operator<=>` with memberwise comparison: ✅ Implemented
- Synthesized comparison operators (==, !=, <, >, <=, >=) from defaulted `<=>`: ✅ Implemented
- Multi-member structs compared in declaration order: ✅ Works
- Nested struct member delegation (calls inner `<=>` for struct members): ✅ Implemented
- Inline expression use `(a <=> b) < 0` in ternary/if/comparisons: ✅ Implemented
- Mixed member types (int + char + short + long long): ✅ Works
- Signed/unsigned member comparisons: ✅ Correct (uses `isSignedType()`)
- User-defined `operator<=>` with custom return types: ✅ Works
- Reversed operand order and self-comparison: ✅ Works
- Template struct `operator<=>`: ✅ Works
- `std::strong_ordering`/`std::weak_ordering`/`std::partial_ordering` return types: ✅ Works in
  reduced tests (manual ordering stand-ins); the real `<compare>` header currently fails
  (verified 2026-11-09), so it cannot be used directly

**Modern Syntax (A)**
- Constexpr if: `if constexpr (condition)` with nesting
- Range-based for loops: `for (auto x : container)` with arrays, begin/end iterators, const refs
- Range-based for with initializer: `for (int i = 0; auto x : container)`
- Spaceship operator `<=>`: Complete (see above)
- Designated initializers: Full support
- Auto type deduction: Basic patterns working
- Structured bindings: `auto [a, b] = expr;`
- Using declarations and aliases: Full support (namespace-level; member using-declarations for inheritance not yet supported — see Known Issues)
- Enum classes: Full support
- constinit variables: Full support (global and local static)
- consteval functions and immediate-invocation enforcement: Full support

**Type System (A)**
- Type traits: 37+ compiler intrinsics (`__is_integral`, `__is_void`, etc.)
- Type properties: const, volatile, signed/unsigned traits
- Type relationships: `__is_same`, `__is_base_of`, `__is_convertible`
- Array traits: bounded/unbounded array detection
- RTTI: `dynamic_cast`, `typeid` support
- Pointer types: multi-level pointers, pointers to members
- Function pointers: Full support
- Member function pointers: Full support
- Reference types: lvalue and rvalue references

**Control Flow (A)**
- All loops: for/while/do-while (with C++20 init statements)
- Range-based for: Arrays and user-defined iterator types
- Switch statements: case/default labels, fallthrough
- Goto: Basic support
- Exception handling: try/catch/throw with multiple handlers, rethrow, nested try-catch
- Return statements: With and without expressions
- Break/continue: Full support

**Exception Handling (A)**
- Try/catch/throw: Full implementation
- Multiple catch handlers and catch-by-type matching
- Nested try-catch blocks
- Rethrow (`throw;`)
- LSDA (.gcc_except_table) generation
- .eh_frame / DWARF CFI generation
- Funclet-style catch block codegen
- Integration with `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`

**OOP Features (A)**
- Classes and structs: Full support
- Inheritance: Single, multiple, virtual inheritance
- Virtual functions: Supported with vtables; `override` validation currently fails for a
  function that overrides through a base with virtual inheritance (see Known Issues)
- Abstract classes: Pure virtual functions
- Constructors and destructors: Full support
- Conversion operators: Full support
- Static members: Full support
- Const member functions: Full support
- Access specifiers: public/protected/private
- `final` keyword: Full support (class-level and method-level)

**Variadic Functions (A)**
- System V AMD64 ABI: Fully implemented
- Register save area for integer and floating-point registers
- va_list structure initialization
- va_arg with integer, floating-point, and struct arguments (≤8 bytes and 9–16 bytes)
- Overflow to stack arguments
- `__VA_OPT__` preprocessor macro: Fully implemented

### Partially Implemented ⚠️

**Variable Template Partial Specialization (100% complete)**
- Structural pattern matching for multi-arg and dependent initializers/types: ✅ Works
- Simple type, pointer, reference, rvalue-reference specializations: ✅ Works
- Inner template argument deduction (e.g., `v<pair<T,U>>` deducing T and U): ✅ Works
- Deeply nested patterns with distinct templates (e.g., `v<A<B<T,U>,C<V>>>`): ✅ Works
- Same template in multiple nested positions (e.g., `v<Pair<Pair<A,B>,Pair<C,D>>>`): ✅ Works

**Code Generation Edge Cases**
- Complex template instantiations (90% complete)
  - Basic specializations: Work
  - Complex dependent types: May have issues
- Advanced pack expansion patterns:
  - Simple pack expansion: Works
  - Nested pack expansion: Works for the verified multi-pack lockstep cases
    (`Outer<Inner<Ts>...>`, `Combine<Outer<Ts>..., Inner<Ts>...>`); deeper dependent contexts
    may still fail
  - Pack expansion in complex contexts: Partially works

**Coroutines (20% complete)**
- Keywords recognized: `co_await`, `co_yield`, `co_return`
- Verified 2026-11-09: the `<coroutine>` header itself parses, but any coroutine body still fails.
  `co_await std::suspend_never{}` errors with `Missing identifier` on the operand, and
  `co_yield 1;` errors with `Missing identifier: co_yield`.
- Full expression parsing and code generation not implemented
- Coroutine frame management not implemented
- Awaitable type checking not implemented
- Required for: usable `<coroutine>` support (a clean header include is not sufficient)

**Standard Library Support**
- The per-header matrix lives in `tests/std/README_STANDARD_HEADERS.md`, but that table is
  stale relative to the current branch and must be re-run before being trusted. Several headers
  it lists as passing (`<limits>`, `<tuple>`, `<utility>`, `<compare>`, `<new>`, `<exception>`)
  now fail, and several it lists as parse/link errors now crash with stack overflow.
- Windows/MSVC mode (bare `#include <header>` + empty `main`, verified 2026-11-09 on MSVC STL
  14.44.35207): 21 of 71 probes compile. Compiling: the C wrappers (`<cstddef>`, `<cstdint>`,
  `<cstdlib>`, `<cstring>`, `<climits>`, `<cstdio>`, `<cmath>`, `<cassert>`, `<cerrno>`,
  `<cfloat>`), plus `<type_traits>`, `<initializer_list>`, `<concepts>`, `<coroutine>`, `<bit>`,
  `<source_location>`, `<version>`, `<ratio>`, `<numbers>`, `<mdspan>`, and `<generator>`.
- Windows/MSVC first stops for the remaining headers: `bad_any_cast during template struct
  parsing` (`<limits>`, `<vector>`, `<deque>`, `<list>`, `<set>`, `<ranges>`, `<chrono>`,
  `<thread>`, `<mutex>`, `<future>`, `<shared_mutex>`), `Fatal error: canonical type:
  conflicting record property publication` (`<tuple>`, `<map>`, `<functional>`,
  `<scoped_allocator>`), missing intrinsics (`_InterlockedCompareExchange128` in `<atomic>`,
  `<barrier>`, `<latch>`; `_BitScanReverse64` in `<charconv>`), `__std_type_info_name`
  (`<any>`, `<memory>`), and stack-overflow crashes (`<array>`, `<optional>`, `<variant>`,
  `<string>`, `<string_view>`, `<iostream>`, `<istream>`, `<ostream>`, `<sstream>`,
  `<fstream>`, `<span>`, `<algorithm>`, `<numeric>`, `<iterator>`, `<queue>`, `<stdexcept>`,
  `<filesystem>`, `<regex>`, `<format>`, `<system_error>`).
- Linux/GCC mode: not re-verified in this audit. A WSL clang++/libstdc++13 build failed even
  `<type_traits>` with `Template instantiation iteration limit exceeded`, so the previous
  "55/68 headers (81%)" figure is unverified and should be treated as stale.
- `<type_traits>`: Good support (most common traits work, 37+ intrinsics)
- `<utility>`: including `<utility>` itself currently stops at MSVC `utility:10081`
  (`explicit specifier is not a constant expression`); `std::forward`, `std::move`, and
  `std::swap` remain covered by reduced tests and paths that do not pull in that constructor.

### Not Implemented ❌

**Modules (0% complete)**
- No `import` keyword support
- No `module` keyword support
- No module dependency tracking
- No module interface/unit separation
- Traditional header-based compilation only
- Required for: C++20 module system
- Verified 2026-11-09: `export module mymodule;` is rejected with
  `Unexpected token in type specifier: 'export'`.

**Ranges Library (range views/adaptors)**
- Range concepts: Implemented (via `<concepts>`)
- Range views/adaptors: Not implemented
  - `std::views::transform`
  - `std::views::filter`
  - `std::views::take`
  - `std::views::drop`
  - Other adaptors
- Range algorithms: Not implemented
  - `std::ranges::for_each`
  - `std::ranges::find`
  - `std::ranges::count`
  - Other algorithms
- Verified 2026-11-09: `#include <ranges>` itself fails during parsing on Windows/MSVC
  (`bad_any_cast during template struct parsing`, then `Unsupported template declaration type`),
  so no adaptor or algorithm can be used yet.

**Advanced C++20 Features**
- Comparison category types enforcement:
  - `std::strong_ordering` parsing: works with reduced stand-ins; the real `<compare>` header
    currently fails (verified 2026-11-09)
  - Compiler-synthesized comparison categories: Not auto-selected
- Some constexpr evaluation edge cases
- Constexpr `std::string` and `std::vector`: unusable because `<string>` currently fails/crashes;
  there are no dedicated constexpr-string tests
- Advanced SFINAE patterns (very complex cases)

**MSVC Standard Library Compatibility**
- Verified 2026-11-09: 21/71 bare-include probes compile. Most failures are now template/sema
  internal errors, missing intrinsics, or stack-overflow crashes rather than SAL macro parsing.
- Current top blockers: `bad_any_cast during template struct parsing` (`<limits>` and most
  containers), `conflicting record property publication` (`<tuple>`, `<map>`, `<functional>`),
  `_InterlockedCompareExchange128` / `_BitScanReverse64` intrinsics, `__std_type_info_name`,
  and unbounded alias/template materialization recursion (stack overflow in
  `<array>`/`<optional>`/`<variant>`/`<string>`/`<span>`).
- SAL annotations and UCRT formatted I/O wrappers are no longer the first stop for these probes.

---

## Implementation Priorities

### High Priority (Blocking Real-World Code)

1. **Standard Library Failures (verified 2026-11-09, Windows/MSVC)**
   - Fix the `bad_any_cast during template struct parsing` internal error that first stops
     `<limits>` and most container headers
   - Fix `canonical type: conflicting record property publication` (`<tuple>`, `<map>`,
     `<functional>`)
   - Bound alias/template materialization recursion to remove stack-overflow crashes
     (`<array>`, `<optional>`, `<variant>`, `<string>`, `<span>`)
   - Model MSVC intrinsics `_InterlockedCompareExchange128` and `_BitScanReverse64`

2. **MSVC Standard Library Compatibility**
   - Improve SAL annotation handling
   - Fix UCRT wrapper call resolution
   - Improve MSVC STL compatibility

3. **Constexpr Evaluation**
   - Improve constexpr evaluation engine
   - Support constexpr in more contexts

### Medium Priority (Important Features)

4. **Coroutines**
   - Implement coroutine frame allocation
   - Implement awaitable type checking
   - Add coroutine-specific code generation
   - Support `co_await`, `co_yield`, `co_return` fully

5. **Ranges Library**
   - Implement range adaptors (views)
   - Implement range algorithms
   - Add range constraint checking

6. **Code Generation Edge Cases**
   - Improve complex template instantiation reliability
   - Stabilize pack expansion in all contexts

### Low Priority (Advanced Features)

7. **Modules**
   - Implement `import`/`export` keywords
   - Add module dependency tracking
   - Implement module interface parsing
   - Support module partitions

8. **Advanced Metaprogramming**
   - Support more complex SFINAE patterns
   - Improve template instantiation performance
   - Add better error messages for template failures

9. **Implicit Conversion in Non-Subscript Operators**
   - Extend the sema selection infrastructure (currently used for built-in subscript)
     to other built-in operators that may require implicit pointer/array conversion.
   - Unify the general user-defined conversion selection path across sema and codegen
     so that all implicit-conversion-sequence selection is owned entirely by sema and
     codegen only emits the already-selected conversion.
   - Verified 2026-11-09 still present: a class with `operator int*()` used as `obj + 1`
     is rejected with `Operator+ not defined for operand types`, while subscript `obj[1]`
     works when the same class returns `int*`.

10. **Conversion Operators Returning Array References**
    - Array-reference-returning conversion function templates now participate in
      call-argument conversions, binding array-reference parameters and decaying
      through the `ArrayToPointer` tail for pointer parameters.
    - Non-template conversions returning array references now bind array
      references and initialize pointers using the returned array address.
      Built-in subscripting accepts these conversions in either operand position
      and preserves the inner bounds of multidimensional arrays.
    - Built-in subscripting also considers conversion function templates whose
      declared return type supplies a concrete pointer or array-reference
      target. Template deduction fills defaults and checks constraints before
      ranking those candidates.

---

## Grammar Coverage Analysis

### Expression Grammar: 97% Complete

**Implemented:**
- All binary operators (precedence 3-17)
- All unary operators
- All postfix operators
- Primary expressions (literals, identifiers, lambdas)
- C++ casts (static, dynamic, const, reinterpret)
- Ternary conditional operator
- Spaceship operator `<=>`
- Fold expressions
- Lambda expressions (including captures, constexpr lambdas, template lambdas)
- Requires expressions
- Range-based for loop syntax

**Partially Implemented:**
- Some complex expression patterns may fail
- Coroutine expressions (co_await, co_yield, co_return)

### Declaration Grammar: 100% Complete

**Implemented:**
- All declaration forms
- Function declarations and definitions
- Variable declarations with all initialization forms
- Class and struct declarations (including `final`)
- Template declarations (all forms)
- Concept declarations
- Enum declarations
- Using declarations and aliases
- Namespace declarations
- Out-of-line member definitions
- Out-of-line nested class member definitions
- Conversion operators
- consteval/constexpr/constinit specifiers

### Statement Grammar: 97% Complete

**Implemented:**
- if/else (with C++20 init statements)
- for loops (with C++20 init statements)
- Range-based for loops
- while loops
- do-while loops
- switch statements
- goto and labels
- return statements
- break/continue
- try/catch/throw
- Expression statements
- Compound statements (blocks)

**Partially Implemented:**
- Some complex control flow patterns may have issues

### Template Grammar: 97% Complete

**Implemented:**
- Type parameters (`typename T`, `class T`)
- Non-type parameters (int N, bool B, auto V)
- Template template parameters (`template<typename> class C`)
- Parameter packs (`typename... Args`)
- Template specializations (partial and full)
- Default template arguments
- Variadic templates
- Template member functions
- Template member classes
- Template aliases
- Template argument deduction
- SFINAE
- Out-of-line nested class definitions
- Deeply nested variable template deduction: same template in multiple positions (e.g., `v<Pair<Pair<A,B>,Pair<C,D>>>`)

**Partially Implemented:**
- Some complex template patterns may fail

---

## Code Reuse and Architecture

**Parser Architecture Quality: 8.5/10**

Strengths:
- Unified declaration parsing with context-driven dispatch
- Shared specifier parsing for all declaration types
- RAII scope guards for resource management
- Expression context system for template disambiguation
- Clean 1:1 mapping from C++ grammar to parser functions

Code Reuse Opportunities:
1. **Template argument parsing** - 3+ locations have similar logic (~200 lines to consolidate)
2. **Cast parsing** - C++ casts, C-style casts, functional casts share type parsing (~150 lines)
3. **Member access patterns** - `.`, `->`, `.*`, `->*` could be unified (~100 lines)
4. **Error handling** - Common patterns could be extracted (~300 lines)

---

## Test Coverage

**Total Test Cases: ~4,100** (4,095 `.cpp` files directly under `tests/` — 3,856 `test_*` files,
of which 493 encode expected diagnostics — plus 72 `.cpp` probes in `tests/std/` and 14
multi-TU case directories; counted 2026-11-09).

Categories with Good Coverage:
- Basic arithmetic: ✅
- Control flow: ✅
- Templates: ✅
- C++20 concepts: ✅
- Type traits: ✅
- SFINAE: ✅
- OOP features: ✅
- Exception handling: ✅
- Variadic functions: ✅
- Spaceship operator: ✅
- Range-based for: ✅

Categories with Partial Coverage:
- Standard library headers: ⚠️ (21/71 bare-include probes on Windows/MSVC; Linux/GCC unverified)
- Complex templates: ⚠️
- Constexpr evaluation edge cases: ⚠️

Categories with Poor Coverage:
- Coroutines: ❌
- Modules: ❌
- Ranges library (adaptors/algorithms): ⚠️

---

## How to Update This Document

When updating C++20 conformance status:

1. Update conformance percentages in Summary section
2. Move features between sections (Not Implemented → Partially Implemented → Fully Implemented)
3. Add newly discovered missing features
4. Update test coverage statistics
5. Update priority levels based on blocking impact

When implementing a missing feature:

1. Change status in appropriate section
2. Add implementation notes
3. Update test coverage
4. Document any limitations or known issues
5. Cross-reference with related features

---

## References

**Parser Code**: `src/Parser.cpp` - Main parsing logic
**Parser Types**: `src/ParserTypes.h` - Shared parsing structures
**AST Definitions**: `src/AstNodeTypes.h` - AST node types
**Code Generator**: `src/IrGenerator.h` - AST to IR translation
**IR Converter**: `src/IRConverter.h` - IR to assembly conversion
**ELF Writer**: `src/ElfFileWriter.h` - ELF object file generation (exception tables, DWARF)
**LSDA Generator**: `src/LSDAGenerator.h` - Language-Specific Data Area for exceptions

**Test Directory**: `tests/` - Test suite (~4,100 test cases)
- `tests/cpp20_integration/` - C++20 feature tests
- `tests/std/` - Standard library header tests
- `tests/std/README_STANDARD_HEADERS.md` - Per-header status matrix (re-run before trusting; see Standard Library Support)
- `tests/test_type_traits_intrinsics.cpp` - Type trait intrinsics
- `tests/test_sfinae_*.cpp` - SFINAE test cases
- `tests/test_exceptions_*.cpp` - Exception handling tests
- `tests/test_spaceship_*.cpp` - Spaceship operator tests

---

## Known Issues

### Enum ADL for anonymous namespaces — verified working (2026-11-09)

**Severity**: None observed (previously Low)

Enum ADL (`src/SymbolTable.h`, `lookup_adl` and `lookup_adl_only`) relies on
`TypeInfo::namespaceHandle()` returning the correct enclosing namespace for enum types. This is
set by `add_enum_type()` in `src/AstNodeTypes.cpp`, which receives a `NamespaceHandle` from the
parser.

Enums declared inside named namespaces and enums nested inside class bodies within named
namespaces work (fixed 2026-03-18 — nested enum qualified-name registration walks the full
`struct_parsing_context_stack_`).

The previously unverified anonymous-namespace case now works:

- `tests/test_adl_anonymous_enum_ret0.cpp` passes (unscoped enum in an anonymous namespace).
- The documented example with a scoped `enum class Color` and an unqualified
  `classify(Color::Red)` also compiles and resolves.

Caveat: anonymous-namespace members are injected into the enclosing namespace by an implicit
using-directive, so ordinary unqualified lookup can find `classify` even if ADL fails. The
existing test remains the regression guard; a call that can only succeed through ADL is not
currently covered.

### Member using-declarations for inherited names are only partially supported

**Severity**: Medium (conformance issue, affects idiomatic C++ inheritance patterns)

C++ [namespace.udecl]/1 allows a using-declaration inside a derived class to re-introduce
hidden base-class members:

```cpp
struct Base {
    int getValue(int x) const { return x; }
};

struct Derived : Base {
    using Base::getValue;        // un-hides Base::getValue(int)
    int getValue() const { return 42; }
};

int main() {
    Derived d;
    d.getValue();   // OK — calls Derived::getValue()
    d.getValue(10); // OK — calls Base::getValue(int) via using-declaration
}
```

Verified 2026-11-09: the example above still fails. `d.getValue(10)` reports
`No matching member function for call to 'getValue'` (`NoViableMemberFunctionCall#1705`), while
`d.getValue()` succeeds. The operator-lookup cases listed below continue to pass.

FlashCpp parses `using X::Y;` inside a struct body and registers the member name in the
struct parsing context (`imported_members`). Class-scope using-declarations are now also
retained on `StructDeclarationNode` with their target base spelling and resolved base
`TypeIndex`, and copied to instantiated class declarations. Unary `+`, `-`, `~`, and `->`
lookup consumes that metadata, so a using-declaration can disambiguate same-named base
operators instead of letting every base contribute a candidate.

General member function and member name lookup still does **not**:

- Copy or alias the base-class member function overloads into the derived class's member
  function list
- Interact with the name-hiding logic in `StructTypeInfo::findMemberFunctionRecursive` or
  `searchStructMembers` to allow hidden non-operator overloads to become visible again

Without that remaining work, derived classes that use `using Base::foo;` to bring base
overloads into scope will not have those overloads available for general overload
resolution.

**Fix**: When parsing `using Base::member;` inside a struct body where `Base` is a known base
class, copy the matching base-class `StructMemberFunction` entries into the derived struct's
`member_functions` list (or add a separate `using_imported_members` list consulted during
lookup). Update `findMemberFunctionRecursive` and `searchStructMembers` to include
using-imported members before applying the name-hiding early-return.

### `override` is not validated through virtual inheritance

**Severity**: Medium (rejects valid C++; discovered 2026-11-09 while verifying "virtual
functions: full support")

A member function marked `override` is rejected when the overridden virtual function is reached
through a base class that itself uses virtual inheritance:

```cpp
struct Base {
    virtual int value() const { return 1; }
};

struct Mid : virtual Base {
    int value() const override { return 2; }   // OK
};

struct Leaf : Mid {
    int value() const override { return 3; }   // error: does not override any base class function
};
```

`Leaf::value` overrides `Mid::value` (and `Base::value`), so the program is valid. Removing the
`override` keyword makes it compile and run with correct virtual dispatch (`p->value()` returns
3). Override validation therefore does not collect the base-class virtual functions correctly
when virtual inheritance is in the ancestry.

**Fix**: Include virtual-base members (or their final overriders) in the base-function set
consulted by `override` validation, and add a regression that covers a virtual-base chain.
