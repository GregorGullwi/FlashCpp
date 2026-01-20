# Standard Header Compilation Investigation Summary

**Date:** 2026-01-20  
**Investigator:** GitHub Copilot  
**Status:** Root cause identified, requires significant implementation work

## Executive Summary

The primary blocker preventing standard C++ headers (especially `<type_traits>` and its dependents) from compiling in FlashCpp is a limitation in the constant expression evaluator's ability to handle template function calls within `static_assert` expressions.

## Root Cause Analysis

### The Pattern That Fails

The libstdc++ standard library uses the following pattern extensively in `<type_traits>`:

```cpp
template <typename _Tp, size_t = sizeof(_Tp)>
constexpr true_type __is_complete_or_unbounded(__type_identity<_Tp>) {
    return {};
}

template<typename _Tp>
struct is_copy_assignable {
    static_assert(__is_complete_or_unbounded(__type_identity<_Tp>{}), 
                  "template argument must be a complete class");
};
```

### Why It Fails

1. **Parser Phase (✅ Works)**
   - The parser successfully parses the expression `__is_complete_or_unbounded(__type_identity<_Tp>{})`
   - Brace initialization of templates works: `__type_identity<_Tp>{}`
   - Function calls with brace-initialized arguments work: `func(__type_identity<int>{})`

2. **ConstExpr Evaluation Phase (❌ Fails)**
   - When evaluating `static_assert` during template instantiation, the ConstExprEvaluator tries to evaluate the function call
   - The evaluator performs a symbol table lookup for the function name
   - **Critical Issue:** The lookup for template functions fails
   - The function is not found because:
     - It's a template function requiring instantiation
     - It has default non-type template parameters
     - Template function instantiation is not implemented in the constexpr evaluation context

### Error Message

```
static_assert condition is not a constant expression: Function not found: __is_complete_or_unbounded
```

## Testing Results

### What Works ✅

```cpp
// 1. Brace initialization of template structs
__type_identity<int> x{};

// 2. Function calls with braced arguments (outside static_assert)
func(__type_identity<int>{});

// 3. Static constexpr member access
static_assert(true_type::value, "ok");

// 4. Simple constexpr function calls
constexpr bool func() { return true; }
static_assert(func(), "ok");

// 5. Member access on function return values
struct S { static constexpr bool value = true; };
constexpr S func() { return {}; }
static_assert(func().value, "ok");
```

### What Doesn't Work ❌

```cpp
// 1. Template function calls in static_assert
template<typename T>
constexpr true_type func(__type_identity<T>) { return {}; }
static_assert(func(__type_identity<int>{}).value, "fail"); // ❌

// 2. Template functions with default non-type parameters
template<typename T, size_t = sizeof(T)>
constexpr bool func() { return true; }
static_assert(func<int>(), "fail"); // ❌ (in constexpr context)
```

## Impact Analysis

### Headers Affected

The following headers depend on `<type_traits>` and are blocked by this issue:

- `<utility>` - Uses `__is_complete_or_unbounded` checks
- `<tuple>` - Depends on `<utility>`
- `<variant>` - Depends on `<type_traits>`
- `<optional>` - Depends on `<type_traits>`
- `<any>` - Depends on `<type_traits>`
- `<array>` - Depends on `<type_traits>`
- `<vector>` - Depends on `<type_traits>`
- `<string>` - Depends on `<type_traits>`
- `<memory>` - Depends on `<type_traits>`
- And many more...

### Headers That Might Work

These headers have simpler requirements and might compile:

- `<limits>` - May time out but doesn't use problematic patterns
- `<initializer_list>` - Simple header (but requires compiler magic)
- `<new>` - Simple declarations
- `<typeinfo>` - Simple declarations
- `<compare>` - May work if timeouts are avoided
- `<version>` - Feature test macros only
- `<source_location>` - Simple builtins

## Required Fixes

To properly support standard headers, the following features need to be implemented:

### 1. Template Function Instantiation in ConstExpr Context

**Scope:** Medium-Large  
**Complexity:** High  
**Priority:** Critical

The ConstExprEvaluator needs to:
- Detect when a function call refers to a template function
- Instantiate the template function with the provided template arguments
- Handle deduction of template arguments from function arguments
- Support default template parameters (both type and non-type)

**Implementation Steps:**
1. Extend `evaluate_function_call()` to check if the function is a template
2. Add template argument deduction logic
3. Instantiate the template function
4. Register the instantiated function for evaluation
5. Evaluate the instantiated function body

### 2. Qualified Name Lookup in ConstExpr Context

**Scope:** Small-Medium  
**Complexity:** Medium  
**Priority:** High

The evaluator needs to:
- Handle namespace-qualified function names (e.g., `std::__is_complete_or_unbounded`)
- Look up functions in their declaring namespace
- Support nested namespace lookups

### 3. Implicit Conversion in Static Assert

**Scope:** Small  
**Complexity:** Low  
**Priority:** Medium

Currently, `static_assert` requires an explicit `.value` access or implicit conversion to `bool`. The standard library relies on implicit conversions from `true_type`/`false_type` to `bool`.

## Workarounds

### For Users

1. **Use Simpler Headers:** Focus on C library wrappers (`<cstddef>`, `<cstdio>`, etc.) which work
2. **Avoid Template-Heavy Headers:** Headers like `<type_traits>`, `<utility>`, `<tuple>` won't work
3. **Manual Implementation:** Implement needed type traits manually without `static_assert` checks

### For Implementation

1. **Stub Out Checks:** Replace problematic `static_assert` calls with no-ops (not recommended)
2. **Simplified Headers:** Create FlashCpp-specific versions of standard headers without checks
3. **Incremental Approach:** Fix one pattern at a time, starting with simpler cases

## Recommendations

### Short Term (1-2 weeks)

1. Document this limitation clearly in the main README
2. Create a list of known-working headers
3. Test and document which simple headers (like `<new>`, `<typeinfo>`) actually work
4. Add more builtin function support to help simpler headers

### Medium Term (1-2 months)

1. Implement template function instantiation in constexpr context
2. Add qualified name lookup support in evaluator
3. Test with simplified versions of standard headers

### Long Term (3+ months)

1. Full constexpr evaluation support including:
   - Template function instantiation
   - Default template parameters
   - Implicit conversions
   - Complex constexpr logic
2. Comprehensive standard library header support
3. Performance optimizations for template-heavy code

## Conclusion

While FlashCpp has made impressive progress in C++20 feature support, the current limitation in constexpr evaluation of template functions prevents compilation of most modern standard library headers. This is a fundamental architectural issue that requires significant implementation work to resolve.

The good news is that:
- The parser works correctly
- The code generation is sound
- C library wrappers and simple headers work
- The architecture supports the needed features

The work required is well-defined and achievable, but represents a substantial engineering effort that should be prioritized based on project goals and resource availability.

## References

- `tests/std/README_STANDARD_HEADERS.md` - Current status documentation
- `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md` - Feature tracking
- `src/ConstExprEvaluator.h` - Current evaluator implementation
- libstdc++ source code - Reference implementation of standard library patterns
