# Template Struct Destructor Fix Plan

**Issue:** Destructors not called for template struct instantiations
**Test Case:** `tests/test_ctad_struct_lifecycle_ret0.cpp`
**Status:** Active Investigation
**Date:** 2026-01-30

## Problem Description

Variables of template struct types (e.g., `TupleLike<int, double> pair(7, 3.5)`) do not have their destructors called when they go out of scope. The test expects destructors to be invoked, but they are never registered in the scope stack for automatic cleanup.

### Expected Behavior
```cpp
TupleLike pair(7, 3.5);  // Constructor called
// ... use pair ...
// Destructor should be called when pair goes out of scope
```

### Actual Behavior
- Constructor is called correctly
- Destructor is never called
- Test returns 3 instead of expected 0
- Global counter `g_dtor_count` remains 0

## Root Cause Analysis

### Investigation Summary

Through extensive debugging with logging at multiple levels:

1. **Template destructors ARE being generated**
   - `is_destructor=1` flag is set correctly in member_functions
   - Destructor IR code is created (`_ZN20TupleLike_int_doubleD1Ev`)
   - Code at `CodeGen.h:4084` (visitDestructorDeclarationNode) executes

2. **TypeInfo exists and is linked**
   - `type_info.struct_info_` pointer is NOT null (e.g., `0x562822927700`)
   - TypeInfo for "TupleLike_int_double" is properly created
   - Lookup in `gTypesByName` succeeds

3. **The critical failure: hasDestructor() returns false**
   ```
   *** Checking destructor for pair, type=TupleLike_int_double, struct_info_=0x562822927700
   *** hasDestructor()=0  // <-- THIS IS THE BUG
   ```

4. **Code path analysis**
   - Variables with brace-initialization take early return at `CodeGen.h:6567`
   - Destructor registration at line 7259 is never reached
   - Even if we add registration before the early return, `hasDestructor()` returns false

### The Core Issue

The StructTypeInfo that `type_info.struct_info_` points to **does not contain the destructor in its member_functions list**, even though:
- The destructor exists in the AST
- The destructor is being visited and generating IR
- The `is_destructor` flag is set on the StructMemberFunction

### Why This Happens

**Hypothesis:** During template struct instantiation in `Parser.cpp`, the TypeInfo→StructTypeInfo linkage is created, but the StructTypeInfo's `member_functions` vector is populated at a different time or in a different order, causing the destructor to be missing when variables are declared.

**Key Code Locations:**

1. **Template Instantiation** (`Parser.cpp`):
   - Line 36647: `instantiate_full_specialization()`
   - Line 37424: `try_instantiate_class_template()` (partial specialization)
   - Line 38840: `try_instantiate_class_template()` (primary template)
   - All call `setStructInfo()` to link StructTypeInfo to TypeInfo

2. **StructTypeInfo Linkage** (`AstNodeTypes.cpp`):
   - Line 86-101: `add_struct_type()` creates TypeInfo
   - StructTypeInfo is created separately and linked via `setStructInfo()`

3. **Member Function Registration**:
   - `StructTypeInfo::member_functions` vector should contain all member functions
   - `findDestructor()` (AstNodeTypes.h:903) searches this vector
   - `hasDestructor()` (AstNodeTypes.h:949) calls `findDestructor()`

## Proposed Solution

### Option 1: Fix Template Instantiation (Recommended)

**Goal:** Ensure StructTypeInfo contains complete member function information including destructors during template instantiation.

**Implementation:**
1. Locate where template struct member functions are added to StructTypeInfo
2. Verify destructor is being added to the `member_functions` vector
3. Add logging to confirm the order of operations:
   - When is TypeInfo created?
   - When is StructTypeInfo created?
   - When are member functions (including destructor) added?
   - When is `setStructInfo()` called?

**Files to Modify:**
- `src/Parser.cpp` - Template instantiation functions
- Specifically around lines 38172, 40370 (where `setStructInfo()` is called)

**Investigation Steps:**
```cpp
// In Parser.cpp, around template instantiation:
std::cerr << "INSTANTIATION: Creating StructTypeInfo for " << instantiated_name << "\n";
// ... populate member_functions ...
std::cerr << "INSTANTIATION: Added " << struct_info->member_functions.size() << " member functions\n";
for (const auto& mf : struct_info->member_functions) {
    std::cerr << "  - is_constructor=" << mf.is_constructor
              << ", is_destructor=" << mf.is_destructor << "\n";
}
```

### Option 2: Workaround in CodeGen (Not Recommended)

Add destructor registration based on whether a destructor was visited, not on `hasDestructor()`.

**Problems:**
- Doesn't fix the root cause
- Fragile and could miss edge cases
- Doesn't solve the underlying type system issue

## Implementation Plan

### Phase 1: Detailed Investigation (1-2 hours)

1. Add logging to template instantiation in Parser.cpp:
   - `try_instantiate_class_template()` at line 38840
   - When `add_destructor()` is called for template structs
   - When member_functions vector is populated

2. Add logging to StructTypeInfo creation:
   - Track when destructors are added to member_functions
   - Verify `findDestructor()` can find them

3. Compare template struct vs regular struct:
   - Regular structs work correctly
   - Identify the difference in instantiation/registration

### Phase 2: Fix Implementation (2-4 hours)

Based on investigation findings, implement the fix in the template instantiation system.

**Likely Fix Location:**
- `Parser.cpp:40370` area where `setStructInfo()` is called
- Ensure `instantiated_struct_ref.add_destructor()` is called (line 41142-41157)
- Verify the added destructor makes it into the final StructTypeInfo

### Phase 3: Testing & Validation (1 hour)

1. Run `test_ctad_struct_lifecycle_ret0.cpp` - should return 0
2. Run full test suite: `./tests/run_all_tests.sh`
3. Verify no regressions (should go from 1 mismatch to 0)

## Code References

### Key Functions

**hasDestructor() Chain:**
```cpp
// AstNodeTypes.h:949
bool hasDestructor() const {
    return findDestructor() != nullptr;
}

// AstNodeTypes.h:903
const StructMemberFunction* findDestructor() const {
    for (const auto& func : member_functions) {
        if (func.is_destructor) {  // <-- This check fails
            return &func;
        }
    }
    return nullptr;  // <-- Returns here for template structs
}
```

**Destructor Registration:**
```cpp
// CodeGen.h:7259 (never reached for brace-init)
// CodeGen.h:6567 (early return path - needs destructor registration added here)
if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
    registerVariableWithDestructor(
        std::string(decl.identifier_token().value()),
        std::string(StringTable::getStringView(type_info.name()))
    );
}
```

### Template Instantiation Code

**Primary Template Instantiation:**
```cpp
// Parser.cpp:38840 - try_instantiate_class_template()
// Around line 40370:
struct_type_info.setStructInfo(std::move(struct_info));

// Around line 41142-41157:
} else if (mem_func.is_destructor) {
    // Handle destructor
    instantiated_struct_ref.add_destructor(
        mem_func.function_declaration,
        mem_func.access,
        mem_func.is_virtual
    );
}
```

## Expected Outcome

After the fix:
- `hasDestructor()` returns true for template struct instances
- Destructors are registered in `scope_stack_`
- `DestructorCall` IR instructions are generated at scope exit
- Test returns 0 (both constructors and destructors called correctly)
- Test suite: 961 valid returns, 0 mismatches

## Related Issues

- Commit bc0f37f: "Fix destructor call ordering in function scopes" - partial fix
- This issue is specifically for template struct instantiations
- Regular structs and non-template classes work correctly

## Session Context

- Session ID: `claude/fix-test-return-value-SSjSO`
- Agent: claude-sonnet-4-5
- Investigation performed: 2026-01-30
- Extensive debugging with multiple approaches:
  - IR generation analysis
  - Type system inspection
  - Code path tracing
  - Logging at multiple levels (FLASH_LOG, std::cerr)

## Next Steps

1. Review this document
2. Implement Phase 1 investigation
3. Based on findings, implement the fix in Parser.cpp
4. Test and validate
5. Commit with descriptive message linking to this document
6. Update EXPECTED_RETURN_VALUES.md to reflect fix
