# Remaining Test Failures Plan

## Current Status (2025-12-31 - Latest)
**795/795 tests passing compilation (100%)**
- All tests compile and link successfully
- Some runtime issues remain in specific test files

### Recent Fixes
- **Fixed**: Mutable lambda captures - by-value captures can now be modified
- **Fixed**: By-reference capture assignment through pointer (DereferenceStore flush bug)
- **Fixed**: test_rvo_very_large_struct.cpp - NOW FULLY PASSING ✅
- Fixed: test_positional_init_only.cpp (brace initialization)
- Fixed: test_lambda_this_capture.cpp (lambda [this] capture)
- Fixed: test_operator_addressof_resolved_ret100.cpp (operator& return type)
- Fixed: test_operator_addressof_overload_baseline.cpp (operator& return type)
- Fixed: test_std_move_support.cpp (std::move with template specialization) ✅
- **Fixed**: test_constructor_expressions.cpp (3/3 pass) - rvalue reference parameter passing ✅
- **Fixed**: test_copy.cpp (2/2 pass) - rvalue reference parameter passing ✅

## Remaining Runtime Issues

### 1. Exception Handling (2 files) - **Not Implemented**
- `test_exceptions_basic.cpp` - Incomplete Linux exception handling support
- `test_exceptions_nested.cpp` - Incomplete Linux exception handling support

**Issue**: Exception handling requires complex runtime support (unwinding, LSDA, personality functions)
**Effort**: Large - requires implementing DWARF exception tables and runtime hooks

### 2. Reference Semantics Issues (1 file remaining) - **Low Priority**
- `test_forward_overload_resolution.cpp` - 2/3 pass, 1 rvalue cast issue remains

**Status**: Core rvalue reference parameter passing fixed. Remaining issue is edge case with specific cast scenarios.
**Effort**: Small - one specific cast type needs fixing

### 3. Virtual Functions/RTTI (2 files) - **Medium Priority**
- `test_covariant_return.cpp` - covariant return types (segfaults at runtime)
- `test_virtual_inheritance.cpp` - virtual inheritance diamond problem (segfaults at runtime)

**Issue**: Complex vtable handling and virtual inheritance offset calculations
**Effort**: Medium-Large - requires vtable thunk generation

### 4. Lambda Features (1 file) - **Partially Fixed** ⚠️
- `test_lambda_cpp20_comprehensive.cpp` - advanced C++20 lambda features

**Fixed Issues**:
- ✅ Mutable lambda captures (`[x]() mutable { x += 2; }`)
- ✅ By-reference capture assignment (`[&y]() { y = x + 2; }`)
- ✅ Mixed captures work correctly

**Remaining Issues**:
- ⚠️ Lambda returning lambda returns wrong value (3 instead of 5)
- ⚠️ Recursive lambda (auto&& self) segfaults

**Effort**: Medium - nested lambda return value semantics need investigation

### 5. Spaceship Operator (1 file) - **Large Effort**
- `spaceship_default.cpp` - defaulted spaceship operator (segfaults at runtime)

**Issue**: Requires synthesizing comparison operations from default <=>
**Effort**: Large - needs proper defaulted operator implementation

### 6. Variadic Arguments (1 file) - **Large Effort**
- `test_va_implementation.cpp` - va_list/va_arg implementation (segfaults at runtime)

**Issue**: Linux va_list implementation differs from Windows
**Effort**: Large - requires proper System V ABI va_list handling

### 7. Access Control Flag (1 file) - **Requires Special Flag**
- `test_no_access_control_flag.cpp` - access control flags

**Issue**: Test requires `-fno-access-control` flag which validation script doesn't pass
**Status**: Works when compiled with correct flag, not a compiler bug

## Recommended Priority Order

1. **Already Fixed** ✅
   - Brace initialization for aggregates
   - Lambda [this] capture offset resolution
   - Operator& return type handling (pointer size)
   - Mutable lambda captures
   - By-reference capture assignment
   - Large struct RVO

2. **Medium Priority (Fix Core Issues)**
   - Lambda returning lambda value semantics
   - Recursive lambda segfault

3. **Lower Priority (Complex Features)**
   - Covariant return types
   - Virtual inheritance

4. **Defer (Major Implementation Work)**
   - Exception handling
   - Defaulted spaceship operator
   - va_list implementation

## Implementation Notes

### ✅ COMPLETED: Mutable Lambda Captures Fix
**Status**: Fully implemented and tested

**Implementation Summary**:
1. **LambdaExpressionNode** (AstNodeTypes.h):
   - Added `is_mutable_` flag to store mutable keyword
   - Added `is_mutable()` getter method

2. **Parser** (Parser.cpp):
   - Pass `is_mutable` flag when constructing LambdaExpressionNode

3. **CodeGen** (CodeGen.h):
   - Added `is_mutable` to LambdaInfo and LambdaContext structs
   - For mutable by-value captures, set LValueInfo metadata with Kind::Member
   - This allows assignment handler to emit MemberStore operations

4. **DereferenceStore Fix** (IRConverter.h):
   - Added `flushAllDirtyRegisters()` before loading values from stack
   - This ensures computed values are written to stack before store_through_ptr

**Test Results**:
- Mutable lambda `[x]() mutable { x += 2; }` returns correct value ✅
- Mixed captures `[x, &y]() { y = x + 2; }` work correctly ✅

---

### ✅ COMPLETED: Reference Parameter Passing Fix (test_constructor_expressions, test_copy)
**Status**: Fully implemented and tested - 5/5 tests passing

**Implementation Summary**:
1. **AddressOf generation** (CodeGen.h):
   - Generate `AddressOf` operation for non-identifier expressions passed to reference parameters
   - Skip duplicate `AddressOf` for cast results that already return 64-bit pointers
   - Mark `AddressOf` results in `reference_stack_info_` to indicate pointer-holding TempVars

2. **TempVar handling** (IRConverter.h):
   - Distinguish TempVars holding objects (use LEA) from those holding addresses (use MOV)
   - Check `reference_stack_info_` to determine correct instruction type
   - Proper handling of pointer dereferencing for struct members

**Test Results**:
- test_constructor_expressions.cpp: 3/3 pass ✅
- test_copy.cpp: 2/2 pass ✅
- test_forward_overload_resolution.cpp: 2/3 pass (1 edge case remains)

**Code Changes**:
- `src/CodeGen.h`: Lines around 11155-11200 (AddressOf generation logic)
- `src/IRConverter.h`: handleTempVar, reference_stack_info_ handling

---

### ✅ COMPLETED: Large Struct RVO Implementation (test_rvo_very_large_struct)
**Status**: Core RVO functionality complete and working

**Implementation Summary**:
1. **RVO Context Tracking** (CodeGen.h):
   - Added `in_return_statement_with_rvo_` flag to track return context
   - Set `ConstructorCallOp.use_return_slot = true` when constructing in return statement
   - Proper detection of when function returns large structs requiring RVO

2. **Return Slot Usage** (IRConverter.h):
   - Load return slot address from `__return_slot` hidden parameter
   - Pass return slot address as 'this' pointer to constructor
   - Constructor builds directly into caller's return slot (eliminates copy)

3. **Stack Allocation Fixes** (IRConverter.h):
   - Fixed `end_offset` calculation for large struct allocations (removed erroneous +8)
   - Added scope extension for pre-allocated TempVars with larger sizes
   - Added return size validation to ensure proper stack allocation

4. **Float Array Support** (IRConverter.h):
   - Detect float/double element types using `ArrayStoreOp.element_type`
   - Load float values into XMM0 register instead of general purpose RDX
   - Generate MOVSS (float32) and MOVSD (float64) store instructions
   - Applied to all array store code paths (constant/variable/named indices)

5. **Member Offset Fix** (CodeGen.h):
   - Set `lvalue_info.offset` to `member->offset` in generateArraySubscriptIr
   - ArrayStore operations now correctly add member offset to base address
   - values array at offset 0, floats array at offset 80

6. **Array Element Size Fix** (CodeGen.h):
   - Detect when array subscript is on TempVar from member_access
   - Compare `element_size_bits` from TempVar with base type size
   - Use base type size when element_size is larger (indicates total array size)
   - Fixed: values[19] uses correct offset 76 bytes instead of 1520 bytes

7. **Struct Size Overflow Fix** (AstNodeTypes.h, Parser.cpp):
   - Changed `TypeSpecifierNode.size_` and `TypeInfo.type_size_` from unsigned char to int
   - Prevents overflow for structs larger than 255 bits (31 bytes)
   - VeryLargeStruct (960 bits) now handled correctly instead of overflowing to 192 bits

8. **Struct Copy Logic** (IRConverter.h):
   - Added struct type handling in handleVariableDecl
   - Implements 8-byte chunk copying for large struct assignments
   - Properly copies 120 bytes from return slot to destination variable
   - Detects struct pointers by checking size_in_bits==64 && var_type==Struct

**Test Results** (as of latest run):
- ✅ No crashes - test runs to completion
- ✅ Constructor count: 1 (expected: 1)
- ✅ Copy count: 0 (expected: 0 with RVO)
- ✅ First value: 100 (expected: 100)
- ✅ Last value: 119 (expected: 119) - **NOW PASSING**
- ✅ First float: 150.0 (expected: 150.0) - **NOW PASSING**

**Code Changes**:
- `src/CodeGen.h`: Lines ~6800-6850 (RVO context), ~11900-12000 (member offset, array size)
- `src/IRConverter.h`: Lines ~3200-3300 (RVO slot), ~6800-7000 (float stores), ~9100-9200 (struct copy)

---

### Reference Parameter Passing Implementation Details
When passing temporary objects or rvalue reference variables to functions:
- Temporary objects like `Widget(42)` are constructed on the stack
- When passed to `void func(Widget&& w)`, the temporary's address should be passed
- **Fixed**: Added AddressOf operation generation for temporaries passed to rvalue reference parameters
- **Fixed**: Proper distinction between TempVars holding values vs addresses in IRConverter

### Large Struct RVO Implementation Details
For structs > 16 bytes:
- Caller must allocate space and pass address as hidden first parameter (`__return_slot`)
- Callee writes directly to that address using RVO
- **Fixed**: Constructor builds directly into return slot
- **Fixed**: Stack frame size calculations for large structs
- **Fixed**: Struct copy from return slot to local variable

---
*Created: 2025-12-30*
*Last Updated: 2025-12-31 (Latest: 795/795 tests passing compilation - 100%)*
*Major Updates:*
- *Rvalue reference parameter passing: COMPLETE (test_constructor_expressions, test_copy now pass)*
- *RVO implementation: COMPLETE (test_rvo_very_large_struct now fully passes)*
- *Mutable lambda captures: COMPLETE (by-value captures can be modified)*
- *By-reference capture assignment: COMPLETE (DereferenceStore flush fix)*
- *Float array stores: COMPLETE (XMM registers, MOVSS/MOVSD instructions)*
- *Array element size calculation: COMPLETE*
- *Struct size overflow: COMPLETE (unsigned char → int)*
