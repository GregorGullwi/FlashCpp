# Missing Features and C++20 Conformance

This document tracks missing C++20 features and implementation gaps in FlashCpp.

**Last Updated**: 2026-02-27
**Overall C++20 Conformance Grade**: **A**
- **Parser**: 97% complete
- **AST**: 97% complete
- **Code Generation**: 90% complete
- **Standard Library**: 81% of headers compile on Linux/GCC mode

## Summary

**Core C++20 Parsing**: Nearly complete! The parser has excellent coverage of C++20 features including concepts, requires expressions, templates, and modern syntax. Most missing features are in code generation edge cases and advanced language constructs.

**Status**: All critical parsing features for C++20 are implemented. Main remaining gaps are:
1. Coroutines (keywords recognized, parsing incomplete)
2. Modules (not implemented)
3. Some code generation edge cases
4. Advanced standard library features (ranges, coroutines)

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
- `std::strong_ordering`/`std::weak_ordering`/`std::partial_ordering` return types: ✅ Works (`<compare>` header compiles)

**Modern Syntax (A)**
- Constexpr if: `if constexpr (condition)` with nesting
- Range-based for loops: `for (auto x : container)` with arrays, begin/end iterators, const refs
- Range-based for with initializer: `for (int i = 0; auto x : container)`
- Spaceship operator `<=>`: Complete (see above)
- Designated initializers: Partial support (see Partially Implemented)
- Auto type deduction: Basic patterns working
- Structured bindings: `auto [a, b] = expr;`
- Using declarations and aliases: Full support
- Enum classes: Full support
- constinit variables: Full support (global and local static)

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
- Virtual functions: Full support with vtables
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

**Variable Template Partial Specialization (95% complete)**
- Structural pattern matching for multi-arg and dependent initializers/types: ✅ Works
- Simple type, pointer, reference, rvalue-reference specializations: ✅ Works
- Inner template argument deduction (e.g., `v<pair<T,U>>` deducing T and U): ✅ Works
- *Known limitation*: Very deeply nested template patterns (e.g., `v<A<B<T,U>,C<V>>>`) are not yet supported

**Code Generation Edge Cases**
- Complex template instantiations (90% complete)
  - Basic specializations: Work
  - Complex dependent types: May have issues
- Designated initializers (92% complete)
  - Basic designated init `{.x = 10, .y = 20}`: ✅ Works
  - Partial init with defaults `{.y = 5}` (omitted fields use default member values): ✅ Implemented
  - Nested designated init `{.inner = {.a = 1}}`: ✅ Works
  - Explicit type designated init as function arg `func(Point{.x = 1, .y = 2})`: ✅ Works
  - Remaining: implicit designated init as function arguments (`func({.x = 1})` without type name)
- Advanced pack expansion patterns:
  - Simple pack expansion: Works
  - Nested pack expansion: May have issues
  - Pack expansion in complex contexts: May have issues

**Coroutines (20% complete)**
- Keywords recognized: `co_await`, `co_yield`, `co_return`
- Basic parsing skeleton exists
- Full expression parsing and code generation not implemented
- Coroutine frame management not implemented
- Awaitable type checking not implemented
- Required for: `<coroutine>` header support

**consteval Functions (40% complete)**
- Keyword recognized: `consteval`
- Parsed and stored in AST (`is_consteval` flag)
- Treated as `constexpr` during code generation
- Compile-time-only enforcement not implemented (calls at non-constant context are not rejected)

**Standard Library Support**
- Linux/GCC mode (55/68 headers compile, 81%):
  - Compiling: `<limits>`, `<type_traits>`, `<compare>`, `<version>`, `<source_location>`, `<numbers>`, `<initializer_list>`, `<optional>`, `<any>`, `<utility>`, `<concepts>`, `<bit>`, `<string_view>`, `<string>`, `<algorithm>`, `<span>`, `<tuple>`, `<vector>`, `<map>`, `<set>`, `<iostream>`, `<atomic>`, `<new>`, `<exception>`, `<typeinfo>`, `<typeindex>`, `<numeric>`, `<variant>` pending fix, `<cset*>`, `<c*>` C headers, `<barrier>`, `<latch>`, `<stdfloat>`, `<spanstream>`, `<print>`, `<expected>`, `<text_encoding>`, `<stacktrace>`, and more
  - Parse errors remaining: `<ratio>`, `<array>`, `<memory>`, `<functional>`, `<ranges>`, `<chrono>`, `<shared_mutex>`, `<coroutine>`, `<variant>`
- MSVC/Windows mode: Limited support (most headers fail on MSVC-specific constructs)
- `<type_traits>`: Good support (most common traits work, 37+ intrinsics)
- `<utility>`: `std::forward`, `std::move`, `std::swap` work

### Not Implemented ❌

**Modules (0% complete)**
- No `import` keyword support
- No `module` keyword support
- No module dependency tracking
- No module interface/unit separation
- Traditional header-based compilation only
- Required for: C++20 module system

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

**Advanced C++20 Features**
- Comparison category types enforcement:
  - `std::strong_ordering` parsing: works via `<compare>` header
  - Compiler-synthesized comparison categories: Not auto-selected
- Some constexpr evaluation edge cases
- Constexpr `std::string` and `std::vector`
- Advanced SFINAE patterns (very complex cases)

**MSVC Standard Library Compatibility**
- Most MSVC headers fail at parse time due to SAL annotations, `_When_` macros, and UCRT wrapper patterns
- Current top blockers: SAL macro parse path, UCRT formatted I/O wrapper call resolution, MSVC STL `yvals.h` compatibility

---

## Implementation Priorities

### High Priority (Blocking Real-World Code)

1. **Standard Library Remaining Parse Errors**
   - Fix brace-init expression parsing for template types (`Type<Args>{init}`)
   - Fix aggregate brace initialization for template types (`std::array<int,5> arr = {1,2,3,4,5}`)
   - Fix dependent base class resolution in template structs
   - Fix variable template evaluation in `static_assert` contexts
   - Fix `<variant>` struct/class definition parse error

2. **MSVC Standard Library Compatibility**
   - Improve SAL annotation handling
   - Fix UCRT wrapper call resolution
   - Improve MSVC STL compatibility

3. **Constexpr Evaluation**
   - Improve constexpr evaluation engine
   - Support constexpr in more contexts
   - Enforce consteval semantics (compile-time-only)

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
   - Add implicit designated init as function argument support

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

**Partially Implemented:**
- Some complex template patterns may fail
- Deeply nested variable template inner-type deduction (e.g., `v<A<B<T,U>,C<V>>>`)

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

**Total Test Cases: 1280+** (1250+ `.cpp` files in `tests/`, plus 34 standard header tests in `tests/std/`)

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
- Standard library headers: ⚠️ (55/68 on Linux/GCC, limited on MSVC)
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
**Code Generator**: `src/CodeGen.h` - AST to IR translation
**IR Converter**: `src/IRConverter.h` - IR to assembly conversion
**ELF Writer**: `src/ElfFileWriter.h` - ELF object file generation (exception tables, DWARF)
**LSDA Generator**: `src/LSDAGenerator.h` - Language-Specific Data Area for exceptions

**Test Directory**: `tests/` - Test suite (1200+ test cases)
- `tests/cpp20_integration/` - C++20 feature tests
- `tests/std/` - Standard library header tests
- `tests/test_type_traits_intrinsics.cpp` - Type trait intrinsics
- `tests/test_sfinae_*.cpp` - SFINAE test cases
- `tests/test_exceptions_*.cpp` - Exception handling tests
- `tests/test_spaceship_*.cpp` - Spaceship operator tests
