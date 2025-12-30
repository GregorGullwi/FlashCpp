# Remaining Test Failures Plan

## Current Status (2025-12-30)
**781/795 tests passing (98.2%)**
- Fixed: test_positional_init_only.cpp (brace initialization)
- Fixed: test_lambda_this_capture.cpp (lambda [this] capture)
- Fixed: test_operator_addressof_resolved_ret100.cpp (operator& return type)
- Fixed: test_operator_addressof_overload_baseline.cpp (operator& return type)

## Remaining 12 Failures Analysis

### 1. Exception Handling (2 files) - **Not Implemented**
- `test_exceptions_basic.cpp` - Incomplete Linux exception handling support
- `test_exceptions_nested.cpp` - Incomplete Linux exception handling support

**Issue**: Exception handling requires complex runtime support (unwinding, LSDA, personality functions)
**Effort**: Large - requires implementing DWARF exception tables and runtime hooks

### 2. Reference Semantics Issues (3 files) - **Medium Priority**
- `test_forward_overload_resolution.cpp` - rvalue reference variable passed to function
- `test_std_move_support.cpp` - std::move with template specialization
- `test_xvalue_all_casts.cpp` - xvalue handling across cast types

**Issue**: When an rvalue reference variable (which is itself an lvalue) is passed to a function, the code incorrectly dereferences it twice.
**Effort**: Medium - need to fix reference parameter passing logic

### 3. Virtual Functions/RTTI (2 files) - **Medium Priority**
- `test_covariant_return.cpp` - covariant return types
- `test_virtual_inheritance.cpp` - virtual inheritance diamond problem

**Issue**: Complex vtable handling and virtual inheritance offset calculations
**Effort**: Medium-Large - requires vtable thunk generation

### 4. Lambda Features (1 file) - **Medium Priority**
- `test_lambda_cpp20_comprehensive.cpp` - advanced C++20 lambda features

**Issue**: Multiple issues including compound assignment in lambdas, nested lambda captures
**Effort**: Medium - multiple sub-issues to fix

### 5. Spaceship Operator (1 file) - **Large Effort**
- `spaceship_default.cpp` - defaulted spaceship operator

**Issue**: Requires synthesizing comparison operations from default <=>
**Effort**: Large - needs proper defaulted operator implementation

### 6. RVO/NRVO (1 file) - **Medium Priority**
- `test_rvo_very_large_struct.cpp` - large struct RVO/NRVO

**Issue**: RVO with large structs (>16 bytes) requires hidden return pointer
**Effort**: Medium - fix hidden return slot handling

### 7. Variadic Arguments (1 file) - **Large Effort**
- `test_va_implementation.cpp` - va_list/va_arg implementation

**Issue**: Linux va_list implementation differs from Windows
**Effort**: Large - requires proper System V ABI va_list handling

### 8. Access Control Flag (1 file) - **Requires Special Flag**
- `test_no_access_control_flag.cpp` - access control flags

**Issue**: Test requires `-fno-access-control` flag which validation script doesn't pass
**Status**: Works when compiled with correct flag, not a compiler bug

## Recommended Priority Order

1. **Already Fixed** ✅
   - Brace initialization for aggregates
   - Lambda [this] capture offset resolution
   - Operator& return type handling (pointer size)

2. **Medium Priority (Fix Core Issues)**
   - Fix reference parameter passing for rvalue refs
   - Fix large struct RVO

3. **Lower Priority (Complex Features)**
   - Lambda compound assignment in captures
   - Covariant return types
   - Virtual inheritance

4. **Defer (Major Implementation Work)**
   - Exception handling
   - Defaulted spaceship operator
   - va_list implementation

## Implementation Notes

### Reference Parameter Passing Fix (test_forward_overload_resolution)
When passing an rvalue reference variable to a function:
- The variable `int&& rref = x;` is stored as a pointer on the stack
- When used as an argument, it should be treated as an lvalue (named reference)
- Current bug: code dereferences the stored pointer, then tries to pass by reference

### Large Struct RVO Fix (test_rvo_very_large_struct)
For structs > 16 bytes:
- Caller must allocate space and pass address as hidden first parameter
- Callee writes directly to that address
- Current issue: may be related to stack frame size or parameter passing

---
*Created: 2025-12-30*
*Last Updated: 2025-12-30 (Session Progress: 777→781 tests passing)*
