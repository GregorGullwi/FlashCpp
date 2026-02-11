# Missing Features and C++20 Conformance

This document tracks missing C++20 features and implementation gaps in FlashCpp.

**Last Updated**: 2026-02-11
**Overall C++20 Conformance Grade**: **A-**
- **Parser**: 95% complete
- **AST**: 95% complete
- **Code Generation**: 85% complete
- **Standard Library**: Partial support

## Summary

**Core C++20 Parsing**: Nearly complete! The parser has excellent coverage of C++20 features including concepts, requires expressions, templates, and modern syntax. Most missing features are in code generation edge cases and advanced language constructs.

**Status**: All critical parsing features for C++20 are implemented. Main gaps are:
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

**Template System (A)**
- Template parameters: type, non-type, template template parameters
- Parameter packs: `typename... Args`
- Non-type template parameters with `auto`: `template<auto V>`
- Template specializations: partial and full
- Fold expressions: `(args + ...)` and all variants
- Template template parameters: `template<typename> class Container`
- Variadic templates: Basic patterns working
- Perfect forwarding: `std::forward` preserves reference types
- SFINAE: Substitution failures handled gracefully
- Out-of-line template member definitions

**Modern Syntax (A)**
- Constexpr if: `if constexpr (condition)` with nesting
- Range-based for with initializer: `for (int i = 0; auto x : container)`
- Spaceship operator `<=>`: Parsing complete
- Designated initializers: Partial support (C-style)
- Auto type deduction: Basic patterns working
- Structured bindings: `auto [a, b] = expr;`
- Using declarations and aliases: Full support
- Enum classes: Full support

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
- Switch statements: case/default labels, fallthrough
- Goto: Basic support
- Exception handling: try/catch/throw (basic patterns)
- Return statements: With and without expressions
- Break/continue: Full support

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

### Partially Implemented ⚠️

**Code Generation Edge Cases**
- Spaceship operator `<=>` (98% complete)
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
  - Template struct `operator<=>`: ✅ Works (is_operator_overload and is_implicit propagated)
  - Remaining: `std::strong_ordering`/`std::weak_ordering`/`std::partial_ordering` return types (requires `<compare>` header)
- Some complex template instantiations (90% complete)
  - Basic specializations: Work
  - Complex dependent types: May have issues
- Designated initializers (92% complete)
  - Basic designated init `{.x = 10, .y = 20}`: ✅ Works
  - Partial init with defaults `{.y = 5}` (omitted fields use default member values): ✅ Implemented
  - Nested designated init `{.inner = {.a = 1}}`: ✅ Works
  - Explicit type designated init as function arg `func(Point{.x = 1, .y = 2})`: ✅ Works
  - Remaining: implicit designated init as function arguments (`func({.x = 1})` without type name) — requires function parameter type inference during expression parsing
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

**Standard Library Support**
- `<type_traits>`: Good support (most common traits work)
  - Primary type categories: 14 intrinsics
  - Composite type categories: 6 intrinsics
  - Type properties: 12 intrinsics
  - Type relationships: 3 intrinsics
  - Total: 37+ intrinsics fully implemented
- `<utility>`: Partial support
  - `std::forward`: Works
  - `std::move`: Works
  - `std::swap`: Basic patterns
  - Other utilities: May be missing
- `<initializer_list>`: Partial support
  - Basic parsing works
  - Constructor overloads: May need work
- Ranges: Concepts supported, adaptors not fully implemented
  - Range concepts (`std::range`, `std::view`): Work
  - Range adaptors (`std::transform_view`, etc.): Not implemented
  - Range algorithms: Not implemented

### Not Implemented ❌

**Modules (0% complete)**
- No `import` keyword support
- No `export` keyword support (for modules)
- No `module` keyword support
- No module dependency tracking
- No module interface/unit separation
- Traditional header-based compilation only
- Required for: C++20 module system

**Ranges Library (Partial)**
- Range concepts: Implemented
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
- consteval functions:
  - Keyword may be recognized
  - Compile-time evaluation not complete
- constinit variables:
  - Keyword may be recognized
  - Constinit semantics not enforced
- Comparison category types:
  - `std::strong_ordering` parsing may work
  - Full semantics not implemented
- Some constexpr evaluation edge cases
- Constexpr std::string and std::vector
- Advanced SFINAE patterns (very complex cases)

**Preprocessor Limitations**
- Basic macros: Work
- Conditional compilation: Works
- Macro arguments: Work
- Stringification (`#`): Works
- Token pasting (`##`): Works
- Variadic macros: Basic support
- Complex macro expressions: May have issues
- `__VA_OPT__`: Not implemented (C++20)
- Preprocessor recursion detection: Not implemented

**Standard Library Headers**
Most headers have limited support:
- `<iostream>`: Very basic (cout, cin)
- `<string>`: Partial support
- `<vector>`: Partial support (basic operations)
- `<algorithm>`: Very limited
- `<functional>`: Basic support
- `<memory>`: Basic smart pointer support
- Most other headers: Not tested or not implemented

---

## Implementation Priorities

### High Priority (Blocking Real-World Code)

1. **Code Generation Edge Cases**
   - ~~Fix remaining spaceship operator codegen issues~~ ✅ Defaulted `<=>` and synthesized operators implemented
   - ~~Fix designated initializer edge cases~~ ✅ Default member values for omitted fields implemented
   - Improve complex template instantiation
   - Stabilize pack expansion in all contexts
   - Add spaceship support for non-integral member types
   - Add designated init as function argument support

2. **Standard Library Support**
   - Expand `<type_traits>` to cover all standard traits
   - Implement more `<algorithm>` functions
   - Improve `<string>` and `<vector>` support
   - Add missing containers (`<map>`, `<unordered_map>`, etc.)

3. **Constexpr Evaluation**
   - Improve constexpr evaluation engine
   - Support constexpr in more contexts
   - Fix consteval/constinit parsing and semantics

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

6. **Exception Handling**
   - Improve try/catch error recovery
   - Add better exception type checking
   - Support exception specifications better

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

### Expression Grammar: 95% Complete

**Implemented:**
- All binary operators (precedence 3-17)
- All unary operators
- All postfix operators
- Primary expressions (literals, identifiers, lambdas)
- C++ casts (static, dynamic, const, reinterpret)
- Ternary conditional operator
- Spaceship operator `<=>`
- Fold expressions
- Lambda expressions (including captures)
- Requires expressions

**Partially Implemented:**
- Some complex expression patterns may fail
- Coroutine expressions (co_await, co_yield, co_return)

### Declaration Grammar: 100% Complete

**Implemented:**
- All declaration forms
- Function declarations and definitions
- Variable declarations with all initialization forms
- Class and struct declarations
- Template declarations (all forms)
- Concept declarations
- Enum declarations
- Using declarations and aliases
- Namespace declarations
- Out-of-line member definitions
- Conversion operators

### Statement Grammar: 95% Complete

**Implemented:**
- if/else (with C++20 init statements)
- for loops (with C++20 init statements)
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

### Template Grammar: 95% Complete

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

**Partially Implemented:**
- Some complex template patterns may fail
- Template template parameters in some contexts

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

**Total Test Cases: 600+**

Categories with Good Coverage:
- Basic arithmetic: ✅
- Control flow: ✅
- Templates: ✅
- C++20 concepts: ✅
- Type traits: ✅
- SFINAE: ✅
- OOP features: ✅

Categories with Partial Coverage:
- Standard library headers: ⚠️
- Complex templates: ⚠️
- Exception handling: ⚠️

Categories with Poor Coverage:
- Coroutines: ❌
- Modules: ❌
- Ranges: ⚠️

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

**Test Directory**: `tests/` - Test suite (600+ test cases)
- `tests/cpp20_integration/` - C++20 feature tests
- `tests/test_type_traits_intrinsics.cpp` - Type trait intrinsics
- `tests/test_sfinae_*.cpp` - SFINAE test cases
