# Link Failures Investigation

**Date**: December 6, 2025  
**Status**: Active Investigation  
**Test Results**: 591/598 tests linking successfully (98.83%)

## Summary

Currently tracking 7 link failures across 3 categories. Recent fixes resolved all extern "C" linkage issues (5 tests fixed).

## Active Link Failures (7 tests)

### 1. Template Instantiation Issues (3 tests)

**Problem**: Template instantiations are not generating proper mangled names or symbol definitions.

**Failing Tests**:
- `template_explicit_args.cpp` - Unresolved: `identity_int`, `max_int`
- `template_multi_param.cpp` - Unresolved: `first_int_float`, `second_int_float`
- `test_func_template_multi_param.cpp` - Unresolved: `identity_int`, `first_int_float`

**Root Cause**: When templates are explicitly instantiated (e.g., `identity<int>`), the mangled name generation for the instantiated function may not be happening correctly, or the instantiated function is not being added to the symbol table with the correct mangled name.

**Next Steps**:
1. Verify that `TemplateRegistry::mangleTemplateName()` generates correct names
2. Check that instantiated functions are properly registered with their mangled names
3. Ensure code generation emits the instantiated function definitions

### 2. Namespace Function Issues (2 tests)

**Problem**: Functions in namespaces are not being found during linking, likely due to incorrect mangling or symbol registration.

**Failing Tests**:
- `test_nested_namespace.cpp` - Unresolved: `func`
- `test_global_namespace_scope.cpp` - Unresolved: 10 symbols (various namespace functions and variables)

**Root Cause**: Namespace-qualified names need special mangling. The simplified `getMangledName()` may not handle namespace prefixes correctly, or the Parser may not be pre-computing mangled names for namespace-scoped functions.

**Next Steps**:
1. Check how `NameMangling::generateMangledNameFromNode()` handles namespace prefixes
2. Verify that functions defined in namespaces get proper mangled names during parsing
3. Ensure code generation and symbol lookups agree on namespace mangling format

### 3. Inheritance Member Access (2 tests)

**Problem**: Derived classes can't find members inherited from base classes.

**Failing Tests**:
- `test_inheritance_basic.cpp` - Unresolved: `getX` (called from `Derived3::sumViaMethod`)
- `test_virtual_inheritance.cpp` - Unresolved: `getX` (called from `DerivedWithMethod::sumViaMethod`)

**Root Cause**: When a derived class method calls a base class method, the code generation may be looking for the function in the wrong class scope. The symbol lookup or mangling may not account for the inheritance hierarchy.

**Next Steps**:
1. Verify that base class members are visible in derived class scope during parsing
2. Check that method calls resolve to the correct mangled name (base class version)
3. Ensure code generation emits calls with correct class qualification

## Recent Fixes (December 6, 2025)

**Tests Fixed**: 5 (from 12 failures down to 7)
- ✅ test_extern_c_single.cpp
- ✅ test_variadic_mixed.cpp
- ✅ test_loop_destructor.cpp
- ✅ test_scope_destructor.cpp
- ✅ test_virtual_basic.cpp

### Issue #2: Linkage Inheritance Not Working
- **Root Cause**: Definitions outside `extern "C" {}` blocks didn't inherit linkage from forward declarations inside
- **Fix**: Added linkage inheritance in `parse_function_declaration()` using `lookup_all()` to find forward declarations
- **File**: `src/Parser.cpp` line ~7228-7242

### Issue #1: extern "C" Functions Being Mangled
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
