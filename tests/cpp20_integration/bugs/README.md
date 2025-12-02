# FlashCpp Known Bugs - Minimal Reproductions

This directory contains minimal reproduction cases for bugs discovered during C++20 integration testing.

## Bug List

### 1. Boolean Intermediate Variables Crash
**File**: `bug_bool_intermediate_crash.cpp`  
**Status**: 🔴 CRASH  
**Symptom**: Assertion failure in IRTypes.h when using boolean intermediate variables  
**Impact**: Cannot use boolean variables in complex logical expressions  

### 2. sizeof... Operator Crash
**File**: `bug_sizeof_variadic.cpp`  
**Status**: 🔴 CRASH  
**Symptom**: Segmentation fault during code generation  
**Impact**: sizeof... operator unusable in variadic templates  

### 3. Namespace Symbol Lookup Crash
**File**: `bug_namespace_symbol_lookup.cpp`  
**Status**: 🔴 CRASH  
**Symptom**: Symbol not found assertion in CodeGen.h  
**Impact**: Cannot use variables declared in namespaces  

### 4. Template Specialization Parser Error
**File**: `bug_template_specialization.cpp`  
**Status**: 🟡 PARSE ERROR  
**Symptom**: Parser fails on template<> explicit specialization syntax  
**Impact**: Explicit template specialization not supported  

### 5. if constexpr Link Error
**File**: `bug_if_constexpr.cpp`  
**Status**: 🟡 LINK ERROR  
**Symptom**: Code generation succeeds but linking fails  
**Impact**: if constexpr in templates doesn't work reliably  

## Testing These Bugs

Each file can be tested independently:

```bash
# Test with standard clang++ (should work)
clang++ -std=c++20 bug_bool_intermediate_crash.cpp -o test && ./test
echo $?  # Should print 0 or 10

# Test with FlashCpp (will demonstrate the bug)
../../x64/Debug/FlashCpp bug_bool_intermediate_crash.cpp
```

## Bug Report Format

Each bug file includes:
- Clear description of the bug
- Minimal reproduction code
- Expected behavior (with standard compilers)
- Actual behavior (with FlashCpp)
- Workaround suggestions
- Date discovered

## Priority Assessment

**Critical (Blocking basic functionality)**:
- Boolean intermediate variables (common pattern)
- Namespace symbol lookup (standard C++ feature)

**High (Limiting advanced features)**:
- sizeof... operator (variadic template limitation)
- Template specialization (important metaprogramming tool)

**Medium (Workarounds available)**:
- if constexpr (can use traditional template recursion)

## Contributing

When you find a new FlashCpp bug:
1. Create a minimal reproduction case
2. Add it to this directory following the naming pattern: `bug_description.cpp`
3. Include all required information (see existing files)
4. Update this README with the new bug entry
5. Test that it works with clang++ to confirm it's valid C++20
