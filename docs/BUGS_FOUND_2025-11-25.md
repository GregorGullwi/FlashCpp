# Bug Report - November 25, 2025

This document tracks bugs discovered during feature verification on November 25, 2025.

## Summary

**All bugs have been fixed!** ✅

- **Bug #2**: Out-of-Line Template Member Function Definitions - ✅ **FIXED**
- **Bug #3**: If Constexpr in Templates - ✅ **FIXED**

---

## 🐛 Bug #2: Out-of-Line Template Member Function Definitions Don't Link

**Date Discovered**: November 25, 2025  
**Date Fixed**: November 26, 2025  
**Status**: ✅ **FIXED**  
**Severity**: High  
**Component**: CodeGen (Template Instantiation)

### Description
Out-of-line member function definitions for templates parse successfully and generate object files, but fail at link time with unresolved symbol errors. The template instantiation code doesn't generate the function body for out-of-line definitions.

### Example Code That Failed
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

### Root Cause
During template instantiation, out-of-line member functions registered in the template registry were never retrieved and instantiated. The template instantiation code only copied inline member function definitions.

### Fix Applied
- Added logic in `src/Parser.cpp` to retrieve out-of-line member functions from `gTemplateRegistry` during template instantiation
- Parse function bodies from saved token positions with parameters in scope
- Substitute template parameters in parsed bodies
- Added type substitution for member functions without definitions (fixes parameter types for all template member functions)

### Verification
✅ Object file generated successfully with correct function mangling: `?add@Container_int@@QAHHH@Z` (shows proper int type substitution)

---

## 🐛 Bug #3: If Constexpr in Templates Doesn't Link

**Date Discovered**: November 25, 2025  
**Date Fixed**: November 26, 2025  
**Status**: ✅ **FIXED**  
**Severity**: Medium  
**Component**: CodeGen (Template Instantiation + Constexpr)

### Description
Template functions using `if constexpr` parse successfully and generate object files, but fail at link time with unresolved symbol errors for each template instantiation. The instantiation doesn't generate the function body.

### Example Code That Failed
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

### Root Cause
The `try_instantiate_template_explicit()` function (used when explicit template arguments are provided, e.g., `func<int>()`) only copied function definitions from `get_definition()`. However, template functions use delayed parsing - their bodies are saved as token positions via `set_template_body_position()`, not as parsed ASTs.

### Fix Applied
- Added template body parsing logic to `try_instantiate_template_explicit()` in `src/Parser.cpp`
- Check for `has_template_body_position()` and parse the body from the saved token position
- Temporarily register template parameter types during parsing
- Substitute template parameters in the parsed body
- Clean up temporary type registrations after parsing

### Verification
✅ All three template instantiations now generate code in section=1 (.text section):
- `?get_category_char@@YAD@Z` at offset 0x0
- `?get_category_int@@YAH@Z` at offset 0x90
- `?get_category_double@@YAN@Z` at offset 0x120

---

## Notes

Both bugs were related to template function body generation during instantiation. The fixes ensure that:
1. Out-of-line template member functions are properly instantiated
2. Template functions with explicit template arguments have their bodies parsed and instantiated
3. All template instantiations now generate proper machine code
