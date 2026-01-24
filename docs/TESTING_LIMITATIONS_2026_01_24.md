# Known FlashCpp Limitations Found During Testing

**Date:** 2026-01-24  
**Context:** Adding tests for pre-flight branch fixes

This document tracks limitations discovered while creating test coverage for parser fixes. These represent cases where:
- Clang compiles successfully
- FlashCpp parsing succeeds BUT codegen/runtime fails
- OR FlashCpp parsing fails where it should succeed

## 1. Member Variable Template Usage ❌ Codegen Issue

**Status:** Parsing works, codegen fails  
**Test:** Attempted in `test_member_var_template_ret42.cpp` (usage commented out)

### What Works

```cpp
struct type_traits {
    template<typename T>
    static constexpr bool is_integral_v = false;
    
    template<typename T>
    static constexpr int size_of_v = sizeof(T);
};
// Parser successfully recognizes and logs:
// [INFO][Parser] Registered member variable template: type_traits::is_integral_v
// [INFO][Parser] Registered member variable template: type_traits::size_of_v
```

### What Fails

```cpp
int main() {
    constexpr bool b = type_traits::is_integral_v<int>;  // ❌ Codegen error
    constexpr int s = type_traits::size_of_v<int>;       // ❌ Codegen error
    return 0;
}
```

**Error:**
```
[ERROR][Codegen] Variable 'is_integral_v' not found in symbol table
FlashCpp: src/IRConverter.h:8133: Assertion failed
```

### Root Cause

The parser correctly identifies and registers member variable templates, but the codegen phase doesn't know how to:
1. Instantiate variable templates
2. Look up instantiated variable template specializations
3. Add them to the symbol table for use in expressions

### Workaround

Test only verifies parsing, not usage:
```cpp
int main() {
    // Test passes if the declarations compile
    // The fix was about parsing these member variable templates
    return 42;
}
```

### Implementation Needed

**File:** `src/IRConverter.h` or `src/CodeGen.h`

Need to add:
1. Variable template instantiation logic (similar to function template instantiation)
2. Symbol table lookup for variable template specializations
3. Mechanism to evaluate constexpr variable templates at compile time

---

## 2. Placement New Expression Usage ❌ Parse Issue

**Status:** Declaration parsing works, expression usage fails  
**Test:** `test_placement_new_parsing_ret42.cpp`

### What Works

```cpp
// Operator declarations parse successfully
inline void* operator new(size_t, void* ptr, MyTag) noexcept {
    return ptr;
}
```

### What Fails

```cpp
int main() {
    char buffer[16];
    MyTag tag;
    Point* p = new ((void*)buffer, tag) Point(10, 32);  // ❌ Parse error
    return 0;
}
```

**Error:**
```
error: Failed to parse initializer expression
      Point* p1 = new ((void*)buffer, tag) Point(10, 32);
                       ^
```

### Root Cause

The parser can handle:
- `new Type` - regular new
- `new (address) Type` - placement new with single argument

But fails on:
- `new (address, tag) Type(args)` - placement new with multiple arguments AND constructor arguments

The parser gets confused distinguishing:
- Constructor call: `Point(args)` 
- Placement new: `new (placement_args) Type(constructor_args)`

When both are present, the parser cannot properly parse the parenthesized placement arguments.

### Workaround

Test only verifies operator declarations parse:
```cpp
int main() {
    // Test passes if these declarations compile
    // The fix was about parsing operator new/delete syntax correctly
    return 42;
}
```

### Implementation Needed

**File:** `src/Parser.cpp`

The placement new parsing logic needs enhancement to:
1. Track opening parenthesis depth after `new` keyword
2. Parse comma-separated placement arguments
3. Then continue to parse the type and optional constructor arguments
4. Distinguish `new (a,b) T(c,d)` as placement args `(a,b)` and constructor args `(c,d)`

---

## 3. Nested Type Access in Requires Specializations ❌ Parse Issue

**Status:** Parser error  
**Test:** `test_requires_requires_detection_ret42.cpp`  
**Documentation:** `docs/NESTED_TYPE_ACCESS_REQUIRES_PLAN.md`

### Pattern

```cpp
template<typename Default, template<typename...> class Op, typename... Args>
    requires requires { typename Op<Args...>; }
struct detector<Default, Op, Args...> {
    using type = typename Op<Args...>::type;  // ❌ Parser error
};
```

**Error:**
```
error: Expected ';' after type alias
      using type = typename Op<Args...>::type;
                                         ^
```

See detailed implementation plan in `docs/NESTED_TYPE_ACCESS_REQUIRES_PLAN.md`.

---

## 4. Fold Expressions with Pack Expansion ⚠️ Runtime Issue

**Status:** Parsing works, runtime crashes observed  
**Context:** Encountered while testing pack expansion

### What Was Attempted

```cpp
template<typename... Ts>
constexpr int eval_pack() {
    return (int(sizeof(Ts)) + ...);  // Fold expression
}
```

**Error:**
```
[WARN][Templates] Fold expression pack '' has no elements
[ERROR][Codegen] Fold expression found during code generation
Segmentation fault
```

### Status

- Fold expressions are already tested in other test files
- This specific pattern (in constexpr function) triggered a crash
- Did not pursue further as fold expression tests already exist
- May be related to constexpr evaluation context

### Related Tests

Existing tests that work:
- `test_fold_expressions_ret194.cpp`
- `test_fold_simple_ret192.cpp`
- `test_fold_minimal_ret6.cpp`

---

## Summary

| Issue | Status | Workaround | Priority |
|-------|--------|------------|----------|
| Member var template usage | Codegen fails | Test only parsing | High |
| Placement new expressions | Parse fails | Test only decls | Medium |
| Nested type in requires | Parse fails | Documented plan | High |
| Some fold expr patterns | Runtime crash | Use existing tests | Low |
| Member func trailing requires | Parse fails | Use leading requires | Medium |
| Void constexpr operator= | Hangs/timeout | Use standard order | Medium |

## New Limitations from PR #556 Tests (2026-01-24)

### 5. Member Function Trailing Requires Clause ❌ Parse Issue

**Status:** Parser error  
**Test:** `test_member_func_trailing_requires_ret42.cpp` (clang works, FlashCpp fails)

#### What Works

```cpp
// Requires clause before const works fine
template<typename T>
class Container {
    T value;
    int process() const requires HasSize<T> { return sizeof(T); }
};
```

#### What Fails

```cpp
// Requires clause after const fails
template<typename T>
class Container {
    T value;
    int getSize() const requires HasSize<T> {  // ❌ Parser error
        return sizeof(T);
    }
};
```

**Error:**
```
error: Expected type specifier
  };
    ^
```

**Root Cause:** The parser expects requires clauses before const/noexcept, not after. C++20 allows requires clauses in the trailing position (after const/noexcept).

**Workaround:** Place requires clause before const:
```cpp
int getSize() const requires HasSize<T> { return sizeof(T); }
```

### 6. void constexpr operator= Pattern ⚠️ Hangs

**Status:** Parser hangs/timeouts  
**Test:** `test_void_constexpr_operator_assign_ret42.cpp` (clang works, FlashCpp hangs)

#### Pattern

```cpp
struct Value {
    int data;
    void constexpr operator=(const Value& other) {  // ⚠️ FlashCpp hangs
        data = other.data;
    }
};
```

**Issue:** Parser hangs when parsing `void constexpr operator=()` with specifier after return type.

**Standard Pattern:** `constexpr void operator=()`

**Workaround:** Use standard specifier order:
```cpp
constexpr void operator=(const Value& other) { ... }
```

## Testing Strategy

For features that are partially implemented:
1. Test what works (parsing/declarations)
2. Document what doesn't work in test comments
3. Create implementation plan documents for complex issues
4. Provide workarounds in test code

This ensures:
- ✅ Tests verify the fixes that were implemented
- ✅ Known limitations are documented
- ✅ Tests don't fail due to unrelated missing features
- ✅ Future implementers have clear guidance
