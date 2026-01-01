# Remaining Test Failures Plan

## Current Status (2026-01-01 - Latest)
**795/795 tests passing compilation (100%)**
- All tests compile and link successfully
- Some runtime issues remain in specific test files

### Recent Fixes
- **Fixed**: Init-capture by reference (`[&y = x]` pattern) - NOW FULLY PASSING ✅
- **Fixed**: Recursive lambda (`auto&& self` pattern) - generates operator() call instead of indirect call
- **Fixed**: Lambda returning lambda - was missing hidden return parameter for struct returns
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
- **Fixed**: Vtable inherited function symbols - derived classes now properly inherit base class vtable entries ✅

## Remaining Runtime Issues

### 1. Exception Handling (2 files) - **Link Failure**
- `test_exceptions_basic.cpp` - Missing typeinfo symbols (_ZTIi)
- `test_exceptions_nested.cpp` - Missing typeinfo symbols (_ZTIi)

**Issue**: Exception handling requires runtime support (typeinfo for thrown types, unwinding, LSDA, personality functions)
**Effort**: Large - requires implementing DWARF exception tables and runtime hooks

### 2. Reference Semantics Issues - **Mostly Fixed** ✅
- `test_forward_overload_resolution.cpp` - **PASSES** (3/3 tests work correctly)

**Status**: All reference parameter passing tests now pass. Previously documented issues have been resolved.

### 3. Virtual Functions/RTTI (2 files) - **Partially Fixed**
- `test_virtual_inheritance.cpp` - **PASSES** (returns 46 = 302 % 256, which is correct)
- `test_covariant_return.cpp` - **Partial**: Pointer covariant returns work; reference covariant returns have issues

**Fixed Issues**:
- ✅ Inherited vtable entries: Derived class vtables now correctly inherit base class function symbols
- ✅ Virtual function dispatch through base pointer: Works correctly
- ✅ Multi-level inheritance vtables: Work correctly

**Remaining Issues**:
- Covariant return with **references** returns wrong values (test 4, 5 in covariant return test)
- Covariant pointer returns work correctly (tests 1, 2, 3, 6, 7)

**Effort**: Medium - reference return handling needs investigation

### 4. Lambda Features (1 file) - **Fully Fixed** ✅
- `test_lambda_cpp20_comprehensive.cpp` - advanced C++20 lambda features (returns 135/135 - ALL TESTS PASS)

**Fixed Issues**:
- ✅ Mutable lambda captures (`[x]() mutable { x += 2; }`)
- ✅ By-reference capture assignment (`[&y]() { y = x + 2; }`)
- ✅ Mixed captures work correctly
- ✅ Lambda returning lambda (was returning wrong value due to missing hidden return parameter)
- ✅ Nested lambdas work correctly
- ✅ Generic lambdas with auto parameters
- ✅ Recursive lambda (`auto&& self` pattern)
- ✅ Init-capture by reference (`[&y = x]` pattern) - NOW FIXED!

**Implementation Details for Init-Capture By Reference Fix**:
- For init-capture by reference `[&y = x]`, generate AddressOf operation for the initializer `x`
- Store the address (not the value) in the closure struct member `y`
- In `pushLambdaContext`, populate `capture_types` for init-captures by looking up the initializer's type
- In `handleLValueCompoundAssignment`, add support for `Kind::Indirect` to handle `y += 2` where `y` is a dereferenced pointer
- Generate proper Dereference/Add/DereferenceStore sequence for compound assignments through pointers

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
   - Vtable inherited function symbols (derived classes now properly inherit base class entries)
   - test_virtual_inheritance.cpp - actually passes (302 % 256 = 46)
   - test_forward_overload_resolution.cpp - actually passes

2. **Medium Priority (Fix Core Issues)**
   - Covariant reference returns (pointer returns work, reference returns have issues)

3. **Lower Priority (Complex Features)**
   - Exception handling (link failures due to missing typeinfo)

4. **Defer (Major Implementation Work)**
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
- *Vtable inherited function symbols: COMPLETE (2026-01-01) - derived class vtables now properly inherit base class function symbols*

---

### ✅ COMPLETED: Vtable Inherited Function Symbols Fix (2026-01-01)
**Status**: Fully implemented and tested

**Problem**:
- Derived class vtables were not properly populating slots for inherited virtual functions
- When a derived class didn't override a base class virtual function (e.g., destructor), the vtable slot remained empty
- This caused segfaults at runtime when calling inherited virtual functions

**Root Cause**:
- In `IRConverter.h`, when vtable entries were initialized, only pure virtual functions were getting symbols (`__cxa_pure_virtual`)
- Inherited functions were left as empty strings because they were only populated when processing the function definition
- Derived classes that don't define their own destructor never had a definition to process

**Implementation** (IRConverter.h, handleFunctionDecl):
1. When creating a new vtable, iterate through all vtable entries from `struct_info->vtable`
2. For pure virtual functions: use `__cxa_pure_virtual` symbol
3. For destructors: use `NameMangling::generateMangledNameFromNode(DestructorDeclarationNode)`
4. For regular virtual functions: use `NameMangling::generateMangledName()` with the function's owning struct name
5. The key insight is that `struct_info->vtable` entries point to the correct `StructMemberFunction*` - either the base class's or derived class's depending on whether it was overridden

**Test Results**:
- ✅ test_virtual_inheritance.cpp: PASSES (returns 46 = 302 % 256, as expected)
- ✅ Covariant pointer returns (tests 1, 2, 3, 6, 7): PASS
- ⚠️ Covariant reference returns (tests 4, 5): Still have issues (separate bug)
