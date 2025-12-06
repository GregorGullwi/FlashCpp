# Link Failures Investigation

**Date**: December 6, 2025  
**Status**: Major Progress - Only 6 Remaining Failures  
**Test Results**: 582/595 tests linking successfully (97.8%)

## Summary

Fixed 9 link failures today (from 15 down to 6). The ELF symbol name bug and template/namespace/inheritance issues are now resolved.

## Remaining Link Failures (6 tests)

### Windows Runtime Dependencies (5 tests - Expected to Fail on Linux)

These tests rely on Windows-specific runtime features that don't exist on Linux/ELF:

**Failing Tests**:
- `test_constexpr_lambda.cpp` - References `ExitProcess` (Windows API)
- `test_dynamic_cast_debug.cpp` - References RTTI symbols (`??_R4`) (Windows RTTI format)
- `test_exceptions_basic.cpp` - References `_CxxThrowException` (Windows exception handling)
- `test_exceptions_nested.cpp` - References `_CxxThrowException` (Windows exception handling)
- `test_noexcept.cpp` - References `_CxxThrowException` (Windows exception handling)

**Note**: These tests pass on Windows but will fail on Linux until the compiler implements Linux-specific exception handling and RTTI.

### Genuine Bugs (1 test)

**Failing Tests**:
- `test_delayed_parsing_constructor.cpp` - Codegen bug: Constructor calls member function `getValue()` but treats it as global variable instead of member function call

## Recent Fixes (December 6, 2025)

**Tests Fixed**: 9 (from 15 failures down to 6)
- ✅ template_explicit_args.cpp
- ✅ template_multi_param.cpp  
- ✅ test_extern_c_single.cpp
- ✅ test_func_template_multi_param.cpp
- ✅ test_global_namespace_scope.cpp
- ✅ test_inheritance_basic.cpp
- ✅ test_nested_namespace.cpp
- ✅ test_variadic_mixed.cpp
- ✅ test_virtual_inheritance.cpp

### Issue #0: ELF Symbol Names Not Null-Terminated (FIXED)
- **Root Cause**: `ElfFileWriter` was passing `string_view.data()` directly to ELFIO's C API which expects null-terminated strings. This caused the library to read past the string's end, including source code in symbol names.
- **Fix**: Convert `string_view` to `std::string` before passing to C API using `.c_str()`
- **File**: `src/ElfFileWriter.h` lines 123-125, 789-791
- **Result**: Symbol names now correctly show function names (e.g., `add`) instead of entire source code

### Issue #5: Inherited Member Function Calls Missing Mangled Names
- **Root Cause**: When a derived class method calls a base class method (e.g., `getX()` in `Derived3::sumViaMethod()`), the code generator only checked the current struct's member functions, not base class member functions
- **Fix**: Added recursive base class search in code generator's function lookup (lines 6932-6969 in CodeGen.h)
- **File**: `src/CodeGen.h` 
- **Result**: Calls to inherited methods now use correct mangled names (e.g., `_ZN5Base34getXEv` instead of `getX`)

### Issue #3: Template Instantiations Missing Mangled Names
- **Root Cause**: Template instantiations were not calling `compute_and_set_mangled_name()`, so they didn't have Itanium/MSVC mangled names for code generation
- **Fix**: Added `compute_and_set_mangled_name()` calls after each template instantiation in 6 locations
- **Fix**: Changed `gSymbolTable.insert()` to `gSymbolTable.insertGlobal()` for template instantiations to ensure global visibility
- **Files**: `src/Parser.cpp` lines 17395-17399, 17887-17893, 20002-20004, 20127-20132, 20366-20370, 20477-20483
- **Result**: Template functions now generate with correct mangled names (e.g., `_Z12identity_inti` instead of `identity_int`)

### Issue #4: Namespace Qualified Function Calls Missing Mangled Names
- **Root Cause**: When parsing `A::B::func()`, the FunctionCallNode was created without setting the mangled name from the looked-up FunctionDeclarationNode
- **Fix**: After creating FunctionCallNode for qualified identifiers, check if the function has a mangled name and set it
- **File**: `src/Parser.cpp` lines 9782-9789
- **Result**: Namespace-qualified calls now use mangled names (e.g., `_ZN1A1B4funcEv` for `A::B::func()`)

### Issue #2: Linkage Inheritance Not Working (Previously Fixed)
- **Root Cause**: Definitions outside `extern "C" {}` blocks didn't inherit linkage from forward declarations inside
- **Fix**: Added linkage inheritance in `parse_function_declaration()` using `lookup_all()` to find forward declarations
- **File**: `src/Parser.cpp` line ~7228-7242

### Issue #1: extern "C" Functions Being Mangled (Previously Fixed)
- **Root Cause**: `compute_and_set_mangled_name()` returned early for C linkage without setting any name
- **Fix**: Set unmangled function name for C linkage (no mangling)
- **File**: `src/Parser.cpp` line ~7182-7185

## Reference: Old getMangledName() Implementation

The previous `getMangledName()` in `ObjFileWriter.h` was complex and error-prone. It tried to:
1. Parse function names to detect member functions (using `::`)
2. Generate mangled names on-the-fly for member functions
3. Search through a `function_signatures_` map to find pre-computed manglings
4. Fall back to generating basic manglings when not found

**Problems with the old approach:**
- Mixed responsibility: both lookup and generation
- Fragile string parsing logic for detecting member functions
- Inconsistent behavior when names weren't pre-computed
- Difficult to debug when linkage issues occurred

**Current simplified approach:**
- Parser pre-computes ALL mangled names during parsing
- `ObjFileWriter` just uses the pre-computed names
- Name mangling logic centralized in `src/NameMangling.h`
- C linkage handled explicitly by setting unmangled names

<details>
<summary>Old getMangledName() code (for reference)</summary>

```cpp
std::string getMangledName(std::string_view name) const {
    // Check if the name is already a mangled name (starts with '?')
    if (!name.empty() && name[0] == '?') {
        return std::string(name);
    }

    // Check if this is a member function call (ClassName::FunctionName format)
    size_t scope_pos = name.rfind("::");
    if (scope_pos != std::string_view::npos) {
        std::string_view class_name = name.substr(0, scope_pos);
        std::string_view func_name = name.substr(scope_pos + 2);

        // Convert class_name to mangled format for nested classes
        // "Outer::Inner" -> "Inner@Outer" (reverse order with @ separators)
        std::string mangled_class;
        {
            std::vector<std::string_view> class_parts;
            std::string_view remaining = class_name;
            size_t pos;

            while ((pos = remaining.find("::")) != std::string_view::npos) {
                class_parts.push_back(remaining.substr(0, pos));
                remaining = remaining.substr(pos + 2);
            }
            class_parts.push_back(remaining);

            // Reverse and append with @ separators
            for (auto it = class_parts.rbegin(); it != class_parts.rend(); ++it) {
                if (!mangled_class.empty()) mangled_class += '@';
                mangled_class += *it;
            }
        }

        // Search function_signatures_ for a matching mangled name
        for (const auto& [mangled, sig] : function_signatures_) {
            // ... complex pattern matching logic ...
        }

        // Fallback: generate basic mangled name
        std::string mangled;
        mangled.reserve(1 + func_name.size() + 1 + mangled_class.size() + 7);
        mangled += '?';
        mangled += func_name;
        mangled += '@';
        mangled += mangled_class;

        // Check if this is a constructor
        std::string_view innermost_class = class_name;
        size_t last_colon = class_name.rfind("::");
        if (last_colon != std::string_view::npos) {
            innermost_class = class_name.substr(last_colon + 2);
        }
        if (func_name == innermost_class) {
            mangled += "@@QAX@Z";  // Constructor
        } else {
            mangled += "@@QAH@Z";  // Regular member function
        }
        return mangled;
    }

    // Not a member function - return as-is
    return std::string(name);
}
```
</details>

## Test Results Summary

```
Total: 600 tests
Compilation: 598 success / 0 failed (100%)
Linking: 591 success / 7 failed (98.83%)

Active failures by category:
- Template instantiation: 3 tests
- Namespace functions: 2 tests  
- Inheritance members: 2 tests
```
