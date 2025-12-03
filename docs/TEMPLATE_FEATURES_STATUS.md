# Template Features Implementation Status

This document tracks the implementation status of C++ template features in FlashCpp after the recent improvements.

## ✅ Completed Features

### 1. Type Trait Intrinsics - Basic (COMPLETED)
**Status**: ✅ **FULLY WORKING**

**Implemented Intrinsics**:
- `__is_void(T)` - Check if type is void
- `__is_integral(T)` - Check if type is integral
- `__is_floating_point(T)` - Check if type is floating point  
- `__is_pointer(T)` - Check if type is pointer (including `int*`, `void*`, etc.)
- `__is_lvalue_reference(T)` - Check if type is lvalue reference (e.g., `int&`)
- `__is_rvalue_reference(T)` - Check if type is rvalue reference (e.g., `int&&`)
- `__is_class(T)` - Check if type is class/struct
- `__is_enum(T)` - Check if type is enum

**Compiler Compatibility**:
- Supports both `__is_*` (MSVC style) and `__builtin_*` (GCC/Clang style) prefixes
- Example: Both `__is_void(T)` and `__builtin_is_void(T)` work

**Test File**: `tests/test_type_traits_intrinsics_working.cpp`

**Example**:
```cpp
if (__is_pointer(int*)) {  // true
    // Handle pointer type
}

if (__is_rvalue_reference(int&&)) {  // true  
    // Handle rvalue reference
}
```

### 2. CTAD (Class Template Argument Deduction)
**Status**: ✅ **FULLY WORKING**

**Test File**: `tests/test_ctad_struct_lifecycle.cpp` (enabled in CI)

**Example**:
```cpp
template <typename T, typename U>
struct TupleLike {
    T first;
    U second;
    TupleLike(T lhs, U rhs) : first(lhs), second(rhs) {}
};

template <typename T, typename U>
TupleLike(T, U) -> TupleLike<T, U>;

int main() {
    TupleLike pair(7, 3.5);  // Deduces TupleLike<int, double>
}
```

## ⚠️ Partially Implemented

### 3. Type Trait Intrinsics - Advanced
**Status**: ⚠️ **PARSING WORKS, CODEGEN NEEDS FIXES**

**Affected Intrinsics**:
- Binary traits: `__is_assignable(To, From)`, `__is_base_of(Base, Derived)`
- Variadic traits: `__is_constructible(T, Args...)`, `__is_trivially_constructible(T, Args...)`

**Issue**: Codegen assumes all type arguments are `TypeSpecifierNode` but doesn't handle the additional arguments vector correctly.

**Note**: MSVC implements these traits via template metaprogramming (partial specialization), not compiler intrinsics. Users can implement their own versions.

## ❌ Not Yet Implemented

### 4. Constexpr Global Objects
**Status**: ❌ **NOT IMPLEMENTED**

**Test File**: `tests/test_constexpr_structs.cpp`

**Issue**: Parser doesn't support global `constexpr` object instantiation like:
```cpp
constexpr Point p1(10, 20);  // Error: Failed to parse
```

**Workaround**: Constexpr constructors work, just not global constexpr objects.

### 5. Lambda Init-Capture (C++14)
**Status**: ❌ **NOT IMPLEMENTED**

**Test File**: `tests/test_lambda_cpp20_comprehensive.cpp`

**Issue**: Parser doesn't support init-capture syntax in lambda captures:
```cpp
auto lambda = [x = value]() { return x; };  // Error: Missing identifier
```

**What Works**: Basic lambda captures (`[x]`, `[&x]`, `[=]`, `[&]`) work fine.

### 6. Standard Library Headers
**Status**: ❌ **NOT IMPLEMENTED**

**Test Files**: `tests/test_cstddef.cpp`, `tests/test_cstdio_puts.cpp`

**Issue**: FlashCpp doesn't resolve standard library header paths (`#include <cstddef>`).

**Note**: The issue is path resolution, not the headers themselves. MSVC's `<type_traits>` could work if FlashCpp could find and parse it.

### 7. Variadic Arguments Implementation Details  
**Status**: ⚠️ **COMPILES WITH WARNINGS**

**Test File**: `tests/test_va_implementation.cpp`

**Status**: Compiles but may have runtime issues with `__va_start` intrinsic.

## Already Working Features (from previous work)

- ✅ Variadic Templates (C++11)
- ✅ Fold Expressions (C++17)
- ✅ Template Template Parameters
- ✅ SFINAE core mechanism
- ✅ Custom type traits (user-defined)
- ✅ Concepts (C++20)

## Key Insights

1. **Type Traits Implementation**:
   - MSVC implements type traits like `is_rvalue_reference` using pure template metaprogramming
   - Compiler intrinsics (`__is_*`) are optional optimizations
   - FlashCpp supports intrinsics for compatibility, but users can write template-based traits too

2. **Compiler Compatibility**:
   - MSVC uses `__is_*` prefix
   - GCC/Clang use `__builtin_*` prefix
   - FlashCpp now supports both for maximum compatibility

3. **Lexer Tokenization**:
   - `&&` is tokenized as a single operator token, not two `&` tokens
   - This is important for parsing rvalue references in type traits

## Test Files Summary

| Test File | Status | Notes |
|-----------|--------|-------|
| `test_type_traits_intrinsics_working.cpp` | ✅ PASS | Basic type trait intrinsics |
| `test_ctad_struct_lifecycle.cpp` | ✅ PASS | CTAD with constructors/destructors |
| `test_constexpr_structs.cpp` | ❌ FAIL | Global constexpr objects not supported |
| `test_lambda_cpp20_comprehensive.cpp` | ❌ FAIL | Init-capture not supported |
| `test_cstddef.cpp` | ❌ FAIL | Standard library path resolution |
| `test_cstdio_puts.cpp` | ❌ FAIL | Standard library path resolution |
| `test_va_implementation.cpp` | ⚠️ PARTIAL | Compiles, runtime uncertain |
| `test_type_traits_intrinsics.cpp` | ❌ FAIL | Uses advanced intrinsics not yet supported |

## CI Status

The following test is now enabled in CI:
- `test_ctad_struct_lifecycle.cpp`

The following test is available but not in original expected failures:
- `test_type_traits_intrinsics_working.cpp`
