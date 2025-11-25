# Bug Report - November 25, 2025

This document tracks bugs discovered during feature verification on November 25, 2025.

## Summary

This document tracks **open bugs** that need to be fixed:

- **2 Template Codegen Bugs**: Out-of-line definitions and if constexpr

---

## 🐛 Bug #2: Out-of-Line Template Member Function Definitions Don't Link

**Date Discovered**: November 25, 2025  
**Status**: ❌ **OPEN**  
**Severity**: High  
**Component**: CodeGen (Template Instantiation)

### Description
Out-of-line member function definitions for templates parse successfully and generate object files, but fail at link time with unresolved symbol errors. The template instantiation code doesn't generate the function body for out-of-line definitions.

### Example Code That Fails
```cpp
template<typename T>
struct Container {
    T add(T a, T b);  // Declaration
};

template<typename T>
T Container<T>::add(T a, T b) {  // Out-of-line definition
    return a + b;
}

int main() {
    Container<int> c;
    return c.add(10, 20);  // Should return 30
}
```

### Error Output
```
Compilation: ✅ Success - "Object file written successfully!"
Linking: ❌ Failed
test_out_of_line.obj : error LNK2019: unresolved external symbol "public: __stdcall Container_int::add(...)" (?add@Container_int@@QAH@Z) referenced in function main
test_ool.exe : fatal error LNK1120: 1 unresolved externals
```

### Root Cause Analysis
The parser successfully recognizes out-of-line template member function definitions and stores them. However, during template instantiation, the code generation phase doesn't emit the function body for these out-of-line definitions.

**Likely Issue Location**: 
- `src/Parser.cpp`: Template instantiation code (around line 13000+)
- The code that copies member functions from template patterns may not handle out-of-line definitions
- IR generation for out-of-line template members may be missing

### Workaround
Define template member functions inline within the class body:
```cpp
template<typename T>
struct Container {
    T add(T a, T b) { return a + b; }  // Inline - works fine
};
```

### Investigation Needed
1. Check if out-of-line definitions are stored in the template registry
2. Verify if template instantiation visits out-of-line member functions
3. Check if IR generation is called for out-of-line template members
4. Compare inline vs out-of-line member function handling in template instantiation

---

## 🐛 Bug #3: If Constexpr in Templates Doesn't Link

**Date Discovered**: November 25, 2025  
**Status**: ❌ **OPEN**  
**Severity**: Medium  
**Component**: CodeGen (Template Instantiation + Constexpr)

### Description
Template functions using `if constexpr` parse successfully and generate object files, but fail at link time with unresolved symbol errors for each template instantiation. The instantiation doesn't generate the function body.

### Example Code That Fails
```cpp
template<typename T>
int get_category() {
    if constexpr (sizeof(T) == 1) {
        return 1;
    } else if constexpr (sizeof(T) == 4) {
        return 4;
    } else {
        return 8;
    }
}

int main() {
    int a = get_category<char>();   // Instantiation
    int b = get_category<int>();    // Instantiation
    int c = get_category<double>(); // Instantiation
    return a + b + c; // Should return 13
}
```

### Error Output
```
Compilation: ✅ Success - "Object file written successfully!"
Linking: ❌ Failed
test_if_constexpr.obj : error LNK2019: unresolved external symbol get_category_char referenced in function main
test_if_constexpr.obj : error LNK2019: unresolved external symbol get_category_int referenced in function main
test_if_constexpr.obj : error LNK2019: unresolved external symbol get_category_double referenced in function main
test_ifc.exe : fatal error LNK1120: 3 unresolved externals
```

### Root Cause Analysis
This appears to be the same fundamental issue as Bug #2 (out-of-line definitions). The template instantiation mechanism doesn't generate code for template function bodies when they contain certain constructs.

**Specific to If Constexpr**:
- The `if constexpr` is evaluated at parse time to determine which branch to keep
- However, template instantiation doesn't trigger IR/code generation for the function body
- The symbol is created but no implementation is emitted

### Workaround
Use non-template constexpr functions or regular if statements:
```cpp
// Non-template version works
constexpr int get_category_int() {
    if constexpr (sizeof(int) == 4) {
        return 4;
    } else {
        return 8;
    }
}
```

### Investigation Needed
1. Check if template function instantiation triggers IR generation
2. Verify if `if constexpr` evaluation prevents function body from being generated
3. Compare with inline if constexpr (in class member functions) vs standalone template functions
4. This may be related to Bug #2 - both involve template function body generation

---

## Recommendations

### High Priority
1. **Fix template function instantiation** - Investigate why template function bodies aren't generated for:
   - Out-of-line member definitions
   - Functions containing if constexpr

---

## Notes

This document tracks **open bugs only**. Fixed bugs and working features have been removed to focus on issues that need attention.
