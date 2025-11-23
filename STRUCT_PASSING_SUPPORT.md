# Struct Passing Support in FlashCpp

## Summary
FlashCpp already has **full, working support** for passing structs by value, const reference, and non-const reference. This document describes how the implementation works.

## Test Coverage
This PR adds comprehensive tests to verify the existing functionality:

### test_struct_ref_passing.cpp
Comprehensive test covering:
- Pass by value (struct copied)
- Pass by const reference (read-only access via pointer)
- Pass by non-const reference (out parameters via pointer)
- Mixed parameter types in same function
- First 4 arguments (register passing: RCX, RDX, R8, R9)
- 5th+ arguments (stack passing)
- Large structs (> 2 members)

### test_struct_ref_simple.cpp
Simple focused test demonstrating:
- Const reference parameter
- Non-const reference parameter (modifies struct)
- Proper semantics (changes propagate back to caller)

## Implementation Details

### 1. IR Generation (src/CodeGen.h:4578-4600)
When generating function calls:
```cpp
// If parameter expects a reference but argument is a value:
if (param_type->is_reference() && !type_node.is_reference()) {
    // Generate AddressOf IR instruction to get pointer
    TempVar addr_var = var_counter.next();
    AddressOfOp addr_op;
    addr_op.result = addr_var;
    addr_op.pointee_type = type_node.type();
    addr_op.operand = identifier.name();
    ir_.addInstruction(IrOpcode::AddressOf, std::move(addr_op));
    
    // Pass the address (pointer)
    irOperands.emplace_back(type_node.type());
    irOperands.emplace_back(64);  // Pointer size
    irOperands.emplace_back(addr_var);
}
```

### 2. Function Parameter Tracking (src/IRConverter.h:4842-4850)
During function prologue, reference parameters are tracked:
```cpp
bool is_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_REFERENCE);
if (is_reference) {
    reference_stack_info_[offset] = ReferenceInfo{
        .value_type = param_type,
        .value_size_bits = param_size,
        .is_rvalue_reference = instruction.getOperandAs<bool>(...)
    };
}
```

The `reference_stack_info_` map tracks which stack offsets contain pointers (references) rather than values.

### 3. Member Access (src/IRConverter.h:7702-7768)
When accessing struct members, the code checks if the object is a reference:
```cpp
// Check if this is the 'this' pointer or a reference parameter
if (object_name == "this" || reference_stack_info_.count(object_base_offset) > 0) {
    is_pointer_access = true;
}

if (is_pointer_access) {
    // Load pointer from stack into RCX
    generateMovFromFrame(X64Register::RCX, object_base_offset);
    
    // Load from [RCX + member_offset] (pointer dereference)
    generateMovFromMemory(temp_reg, X64Register::RCX, op.offset);
} else {
    // Direct stack access for value structs
    generateMovFromFrame(temp_reg, member_stack_offset);
}
```

### 4. Name Mangling
The Microsoft Visual C++ name mangling correctly distinguishes:
- **By value**: `VPoint` 
  - Example: `?passByValue@@YAHVPoint@@@Z`
- **Const reference**: `AEBVPoint`
  - A = reference modifier
  - E = extended qualifier
  - B = const lvalue reference
  - V = class/struct type
  - Example: `?passByConstRef@@YAHAEBVPoint@@@Z`
- **Non-const reference**: `AEAVPoint`
  - A = reference modifier
  - E = extended qualifier
  - A = non-const lvalue reference
  - V = class/struct type
  - Example: `?modifyByRef@@YAXAEAVPoint@@@Z`

## x64 Windows Calling Convention
The implementation correctly follows the Windows x64 calling convention:

### Register Passing (First 4 Arguments)
- Integer/pointer args: RCX, RDX, R8, R9
- For struct references: pointer value loaded into register
- For small structs by value (≤8 bytes): struct value in register
- For large structs by value (>8 bytes): hidden pointer in register

### Stack Passing (5th+ Arguments)
- Arguments pushed on stack in right-to-left order
- Each slot is 8 bytes aligned
- References passed as 8-byte pointers

## Testing
All tests compile successfully and generate correct object files:
```bash
$ make test CXX=clang++
$ ./x64/Test/test --test-case="Struct:PassingByReference"
$ ./x64/Test/test --test-case="Struct:ReferencePassingSimple"
```

Both tests pass with correct name mangling and code generation.

## Conclusion
The FlashCpp compiler has complete, production-ready support for struct passing by value, const reference, and non-const reference. The implementation correctly handles:
- ✅ Small and large structs
- ✅ Register and stack parameter passing
- ✅ Const and non-const references
- ✅ Proper pointer semantics for references
- ✅ Correct name mangling
- ✅ Member access on reference parameters

No code changes were needed - only comprehensive tests to document and verify the existing functionality.
