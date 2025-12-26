# decltype Base Class Support Implementation

## Overview

This PR implements support for `decltype` in base class specifications, addressing one of the highest priority missing features identified in `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`.

## Changes Made

### 1. AST Nodes (`src/AstNodeTypes.h`)
- Added `DeferredBaseClassSpecifier` struct to store decltype expressions for deferred evaluation
- Added `deferred_base_classes_` vector to `StructDeclarationNode`
- Added methods: `add_deferred_base_class()`, `deferred_base_classes()`

### 2. Parser (`src/Parser.cpp`)

#### Base Class Parsing
- Modified base class parsing to recognize `decltype(expr)` syntax
- Parse the expression inside decltype without immediate evaluation
- Try to evaluate the expression:
  - **Success (type_index > 0)**: Add as regular base class immediately
  - **Failure (type_index = 0)**: Defer for template instantiation

#### Template Instantiation
- Added code in `try_instantiate_class_template()` to process deferred base classes
- Evaluates deferred decltype expressions during template instantiation
- Adds the resulting base class to the instantiated struct

### 3. Tests
- `test_decltype_base_simple_ret42.cpp`: Non-template struct with decltype base (✓ passes)
- `test_decltype_base_comprehensive_ret42.cpp`: Comprehensive test with documentation

## What Works

### ✅ Non-Template Structs with decltype Base
```cpp
namespace detail {
    struct result_type {
        static constexpr bool value = true;
    };
    
    result_type get_result() { return {}; }
}

struct test_struct : decltype(detail::get_result()) {
    // Inherits 'value' member from result_type
};

int main() {
    test_struct t;
    return t.value ? 42 : 0;  // ✓ Returns 42
}
```

## Known Limitations

### ⚠️ Template-Dependent decltype Expressions

Template-dependent decltype expressions are **parsed and deferred correctly**, but evaluation during template instantiation requires template parameter substitution, which is not yet implemented.

**Example from `<type_traits>` (line 194):**
```cpp
template<typename... _Bn>
struct __or_
  : decltype(__detail::__or_fn<_Bn...>(0))  // ⚠️ Needs template parameter substitution
    { };
```

**What happens:**
1. ✅ Parser correctly identifies this as a decltype base
2. ✅ Expression is stored and deferred until instantiation
3. ⚠️ During instantiation, the expression cannot be evaluated because `_Bn...` hasn't been substituted
4. ⚠️ Base class is not added (type_index remains 0)

**To fully support this pattern, we need to:**
1. Implement template parameter substitution in expression AST nodes
2. Walk the decltype expression tree and replace template parameter references
3. Re-evaluate the expression with concrete types
4. Add the resulting base class

This is a complex feature that affects expression evaluation, not just parsing.

## Testing

- **All 749 existing tests pass** - no regressions introduced
- New test cases added and passing
- Comprehensive test documents working cases and limitations

## Impact on Standard Library Support

This implementation provides:
1. **Foundation** for decltype base class support
2. **Partial support** for non-template code using decltype bases
3. **Clear path forward** for full template-dependent support

For full `<type_traits>` support, the next step is implementing template parameter substitution in expressions, which is documented in the code and test files.

## Code Quality

- Followed existing code style and patterns
- Added comprehensive logging for debugging
- Minimal changes to existing code paths
- Clear separation between parsing and instantiation logic
