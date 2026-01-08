# Template Argument Type Consolidation Plan

## Overview

This document outlines the plan to consolidate duplicate `TemplateArgument` structures and improve type representation consistency across the FlashCpp codebase. The goal is to eliminate code duplication, improve maintainability, and create clearer abstractions for template argument handling.

## Current State Analysis

### Existing Template Argument Representations

**1. `TemplateTypeArg` (TemplateRegistry.h:39)**
- **Purpose:** Most comprehensive representation for template instantiation and pattern matching
- **Fields:**
  - `Type base_type` - Base type enum
  - `TypeIndex type_index` - Index for user-defined types
  - `bool is_reference`, `is_rvalue_reference` - Reference qualifiers
  - `size_t pointer_depth` - Pointer indirection levels
  - `CVQualifier cv_qualifier` - const/volatile qualifiers
  - `bool is_array` - Array type indicator
  - `std::optional<size_t> array_size` - Known array size
  - `MemberPointerKind member_pointer_kind` - Member pointer info
  - `bool is_value` + `int64_t value` - Non-type template parameters
  - `bool is_pack` - Variadic template parameter packs
  - `bool is_dependent` + `StringHandle dependent_name` - Dependent types
- **Usage:** Template instantiation, specialization pattern matching, `substitute_template_parameter()`
- **Strengths:** Complete type information with all qualifiers and modifiers

**2. `TemplateArgument` (TemplateRegistry.h:298)** 
- **Purpose:** Function template argument deduction and instantiation
- **Fields:**
  - `Kind kind` - Type/Value/Template discriminator
  - `Type type_value` - Type enum (legacy, types only)
  - `int64_t int_value` - Integer values for non-type parameters
  - `Type value_type` - Type of non-type value
  - `std::string template_name` - Template template parameter name
  - `std::optional<TypeSpecifierNode> type_specifier` - Full type info (modern)
- **Usage:** Function template instantiation, deduction, mangling
- **Strengths:** Supports template template parameters, has optional full type info

**3. `TemplateArgument` (InstantiationQueue.h:35)** ⚠️ DUPLICATE
- **Purpose:** Instantiation tracking and deduplication
- **Fields:**
  - `Kind kind` - Type/Value/Template discriminator
  - `Type type` - Type enum
  - `TypeIndex type_index` - Index for complex types
  - `unsigned long long value` - Value for non-type parameters
- **Usage:** InstantiationQueue for tracking pending/completed instantiations
- **Strengths:** Simple, has both Type and TypeIndex (unlike TemplateRegistry version)
- **Problem:** Duplicate name causes ambiguity and maintenance issues

**4. `TypedValue` (IRTypes.h:926)**
- **Purpose:** IR-level typed value representation
- **Fields:**
  - `Type type` - Type enum
  - `int size_in_bits` - Size information
  - `IrValue value` - Variant holding actual value
  - `bool is_reference`, `is_signed` - Runtime properties
  - `TypeIndex type_index` - Type index for complex types
  - `int pointer_depth` - Pointer levels
  - `CVQualifier cv_qualifier` - CV qualifiers
- **Usage:** IR generation and code generation
- **Note:** Similar concept but different purpose (runtime vs compile-time)

## Problems Identified

1. **Name Collision:** Two `TemplateArgument` structs in different headers causes ambiguity
2. **Incomplete Information:** TemplateRegistry.h `TemplateArgument` lacks `TypeIndex` for types (only has Type enum)
3. **Redundancy:** InstantiationQueue duplicates functionality that should be shared
4. **Manual Conversions:** Code manually converts between representations instead of using helper functions
5. **Maintenance Burden:** Changes need to be replicated across duplicates

## Consolidation Tasks

### Task 1: Create TemplateArgumentValue Helper Struct ✅ COMPLETED

**Status:** ✅ Implemented and tested (commit 8cee950 + current)

**Goal:** Extract the type+value pattern into a reusable abstraction (different from existing `TypedValue` which is IR-specific)

**Implementation:**
Added to TemplateRegistry.h before TemplateTypeArg (line 36):
```cpp
// Basic type+index+value triple for template arguments
// Provides a lightweight representation that can be reused across different contexts
// This is distinct from TypedValue (IRTypes.h) which is for IR-level runtime values
struct TemplateArgumentValue {
    Type type = Type::Invalid;
    TypeIndex type_index = 0;
    int64_t value = 0;
    
    // Factory methods
    static TemplateArgumentValue makeType(Type t, TypeIndex idx = 0) {
        TemplateArgumentValue v;
        v.type = t;
        v.type_index = idx;
        return v;
    }
    
    static TemplateArgumentValue makeValue(int64_t val, Type value_type = Type::Int) {
        TemplateArgumentValue v;
        v.type = value_type;
        v.value = val;
        return v;
    }
    
    bool operator==(const TemplateArgumentValue& other) const {
        return type == other.type && 
               type_index == other.type_index && 
               value == other.value;
    }
    
    size_t hash() const {
        size_t h = std::hash<int>{}(static_cast<int>(type));
        h ^= std::hash<TypeIndex>{}(type_index) << 1;
        h ^= std::hash<int64_t>{}(value) << 2;
        return h;
    }
};
```

**Verification:**
- ✅ Build successful with clang++
- ✅ All 845 tests pass
- ✅ All 12 fail tests correctly fail
- ✅ No new warnings

**Benefits:**
- Reusable across multiple structures
- Consistent representation of type+index+value triple
- Can be used in both TemplateArgument versions
- Simplifies conversion functions

### Task 2: Enhance TemplateRegistry.h TemplateArgument

**Status:** 🔲 Not started

**Goal:** Add missing `TypeIndex` field to the canonical TemplateArgument to eliminate need for separate InstantiationQueue version

**Changes to TemplateRegistry.h:298:**
```cpp
struct TemplateArgument {
    enum class Kind {
        Type,
        Value,
        Template
    };
    
    Kind kind;
    
    // Type arguments - keep both for backwards compatibility
    Type type_value;  // Legacy: enum only (keep for existing code)
    TypeIndex type_index = 0;  // NEW: Add index for complex types
    
    // Value arguments
    int64_t int_value;
    Type value_type;
    
    // Template template arguments
    std::string template_name;
    
    // Full type info (modern approach)
    std::optional<TypeSpecifierNode> type_specifier;
    
    // Factory methods updated
    static TemplateArgument makeType(Type t, TypeIndex idx = 0) {
        TemplateArgument arg;
        arg.kind = Kind::Type;
        arg.type_value = t;
        arg.type_index = idx;  // NEW
        return arg;
    }
    
    static TemplateArgument makeTypeSpecifier(const TypeSpecifierNode& type_spec) {
        TemplateArgument arg;
        arg.kind = Kind::Type;
        arg.type_value = type_spec.type();
        arg.type_index = type_spec.type_index();  // Extract and store
        arg.type_specifier = type_spec;
        return arg;
    }
    
    // ... rest of methods updated similarly
};
```

**Migration:**
- Update all `makeType()` calls that need TypeIndex to pass it explicitly
- Existing calls work unchanged (idx defaults to 0)
- Update equality and hash methods to include type_index

### Task 3: Remove InstantiationQueue.h Duplicate

**Goal:** Eliminate duplicate TemplateArgument definition

**Changes to InstantiationQueue.h:**
```cpp
// Remove lines 35-88 (entire TemplateArgument struct)

// Add include at top
#include "TemplateRegistry.h"

// Add namespace alias for clarity
namespace FlashCpp {
    // Use canonical TemplateArgument from TemplateRegistry
    // (No using declaration needed - same name in same namespace)
}
```

**Changes to InstantiationQueue.cpp:**
- Verify all usages work with enhanced TemplateRegistry version
- Update any code that relied on InstantiationQueue-specific behavior
- Ensure hash() and operator==() work correctly

**Files to check:**
- `InstantiationQueue.cpp` - Update includes, verify usage
- Any other files including `InstantiationQueue.h` - Should just work

### Task 4: Add Conversion Helper Functions

**Goal:** Provide clean conversions between TemplateArgument and TemplateTypeArg

**Implementation in TemplateRegistry.h:**
```cpp
// Convert TemplateArgument to TemplateTypeArg
inline TemplateTypeArg toTemplateTypeArg(const TemplateArgument& arg) {
    TemplateTypeArg result;
    
    if (arg.kind == TemplateArgument::Kind::Type) {
        if (arg.type_specifier.has_value()) {
            // Modern path: use full type info
            const auto& ts = *arg.type_specifier;
            result.base_type = ts.type();
            result.type_index = ts.type_index();
            result.is_reference = ts.is_reference();
            result.is_rvalue_reference = ts.is_rvalue_reference();
            result.pointer_depth = ts.pointer_levels().size();
            result.cv_qualifier = ts.cv_qualifier();
            // ... copy other fields
        } else {
            // Legacy path: use basic type info
            result.base_type = arg.type_value;
            result.type_index = arg.type_index;
        }
    } else if (arg.kind == TemplateArgument::Kind::Value) {
        result.is_value = true;
        result.value = arg.int_value;
        result.base_type = arg.value_type;
    }
    
    return result;
}

// Convert TemplateTypeArg to TemplateArgument
inline TemplateArgument toTemplateArgument(const TemplateTypeArg& arg) {
    if (arg.is_value) {
        return TemplateArgument::makeValue(arg.value, arg.base_type);
    } else {
        // Create TypeSpecifierNode for full info
        TypeSpecifierNode ts(arg.base_type, arg.cv_qualifier, 
                            get_type_size_bits(arg.base_type), Token());
        ts.set_type_index(arg.type_index);
        
        // Add pointer levels
        for (size_t i = 0; i < arg.pointer_depth; ++i) {
            ts.add_pointer_level(CVQualifier::None);
        }
        
        if (arg.is_reference) {
            ts.set_reference();
        } else if (arg.is_rvalue_reference) {
            ts.set_rvalue_reference();
        }
        
        return TemplateArgument::makeTypeSpecifier(ts);
    }
}
```

**Benefits:**
- Explicit, type-safe conversions
- Single source of truth for conversion logic
- Easy to test and maintain
- Can be used to replace manual conversion code

### Task 5: Update Existing Conversion Code

**Goal:** Replace manual conversions with helper functions

**Locations to update (from previous analysis):**
1. Parser.cpp:25148-25156 - Manual conversion loop
2. Parser.cpp:26133-26140 - Manual conversion loop  
3. Parser.cpp:29897+ - Similar patterns
4. Parser.cpp:30087+ - Similar patterns
5. Parser.cpp:30199+ - Similar patterns
6. Parser.cpp:30331+ - Similar patterns
7. Parser.cpp:30369+ - Similar patterns
8. Parser.cpp:31601+ - Similar patterns

**Example transformation:**
```cpp
// Before:
std::vector<TemplateArgument> converted_template_args;
for (const auto& arg : template_args) {
    if (arg.kind == TemplateArgument::Kind::Type) {
        converted_template_args.push_back(TemplateArgument::makeType(arg.type_value));
    } else if (arg.kind == TemplateArgument::Kind::Value) {
        converted_template_args.push_back(TemplateArgument::makeValue(arg.int_value, arg.value_type));
    }
}

// After:
std::vector<TemplateTypeArg> converted_template_args;
std::transform(template_args.begin(), template_args.end(),
               std::back_inserter(converted_template_args),
               toTemplateTypeArg);
// Or simply: use TemplateArgument directly if Task 2 is complete
```

### Task 6: Consider TemplateTypeArg and TemplateArgument Unification

**Goal:** Evaluate whether these should be merged into a single type

**Analysis:**
- **TemplateTypeArg**: Rich, specialized for instantiation, pattern matching
- **TemplateArgument**: More flexible, supports template template params, used in deduction

**Recommendation:** Keep separate but ensure clean conversion

**Reasoning:**
1. Different purposes: TemplateTypeArg is for concrete instantiation context, TemplateArgument is for deduction/tracking
2. TemplateTypeArg's complexity (dependent types, packs) not needed in all contexts
3. TemplateArgument's template template parameter support not needed in TemplateTypeArg
4. With Task 4 conversion functions, separation is manageable
5. Premature unification could complicate both use cases

**Future consideration:** If usage patterns converge, could revisit unification in Phase 2 refactoring

### Task 7: Update Documentation and Comments

**Goal:** Document the new structure and usage guidelines

**Actions:**
1. Add header comments explaining when to use each type
2. Document conversion functions with examples
3. Update any existing documentation referencing the old structure
4. Add design rationale comments

**Documentation to add to TemplateRegistry.h:**
```cpp
/**
 * Template Argument Type System
 * ==============================
 * 
 * TemplateArgumentValue: Basic type+index+value triple for simple contexts
 * 
 * TemplateArgument: Used for function template deduction and instantiation tracking
 *   - Supports Type, Value, and Template template parameters
 *   - Has both legacy (type_value) and modern (type_specifier) type representation
 *   - Use for: deduction, mangling, instantiation queue
 * 
 * TemplateTypeArg: Rich type representation for template instantiation
 *   - Complete qualifiers (const, volatile, reference, pointer, array)
 *   - Supports dependent types and parameter packs
 *   - Use for: pattern matching, specialization selection, substitute_template_parameter()
 * 
 * Conversion: Use toTemplateTypeArg() and toTemplateArgument() helpers
 */
```

### Task 8: Testing and Validation

**Goal:** Ensure all changes work correctly and don't introduce regressions

**Test Plan:**
1. **Build Verification:**
   - Clean build with clang++
   - Clean build with MSVC (if applicable)
   - No new compiler warnings

2. **Existing Test Suite:**
   - Run all 845 tests
   - Verify all pass (currently: 845/845)
   - Check fail tests still fail correctly (12 tests)

3. **Specific Template Tests:**
   - Template function instantiation tests
   - Template class instantiation tests
   - Partial specialization tests
   - Template template parameter tests
   - Variable template tests

4. **Manual Verification:**
   - Test files using function templates
   - Test files using class templates with specializations
   - Test files with complex template argument patterns

5. **Performance Check:**
   - Compare compilation times before/after
   - Check template instantiation profiling (if available)
   - Verify no performance regressions

## Implementation Order

1. **Task 1** (TemplateArgumentValue) - Foundation, low risk
2. **Task 2** (Enhance TemplateArgument) - Core change, medium risk
3. **Task 3** (Remove duplicate) - Depends on Task 2, medium risk
4. **Task 4** (Conversion helpers) - Depends on Task 2, low risk
5. **Task 8** (Testing round 1) - Verify Tasks 1-4 work
6. **Task 5** (Update conversions) - Incremental cleanup, low risk
7. **Task 7** (Documentation) - Can be done anytime, low risk
8. **Task 8** (Testing round 2) - Final verification
9. **Task 6** (Future evaluation) - Ongoing consideration

## Success Criteria

✅ No duplicate `TemplateArgument` structures in codebase
✅ TemplateRegistry.h version includes TypeIndex field
✅ Clean conversion functions between TemplateArgument and TemplateTypeArg
✅ All existing tests pass (845/845)
✅ No new compiler warnings
✅ Code is more maintainable and less error-prone
✅ Clear documentation of when to use each type

## Rollback Plan

If issues arise:
1. Revert commits in reverse order
2. Keep conversion functions (Task 4) - they're useful regardless
3. Can partially implement (Tasks 1-4) and defer Task 5 cleanup
4. TemplateArgumentValue (Task 1) is standalone and can be kept even if other tasks are reverted

## Future Enhancements

After this consolidation is stable:
1. Consider TemplateTypeArg/TemplateArgument unification (Task 6)
2. Evaluate if TypeSpecifierNode should use TemplateArgumentValue internally
3. Look for other type+value+index patterns in the codebase that could use TemplateArgumentValue
4. Consider constexpr evaluation improvements leveraging the unified types
