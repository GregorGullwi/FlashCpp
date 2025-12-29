# Register Flush Bug: Conditional Branch After Structured Bindings

## Summary

A bug exists in FlashCpp where using structured bindings combined with compound assignment operators (`+=`) before a conditional branch causes incorrect code generation. The conditional branch tests the wrong register value, leading to incorrect program behavior.

## Symptoms

When the following conditions are met, template specialization checks fail incorrectly:
1. Structured bindings are used (`auto [x, y] = ...`)
2. Compound assignment operators are used (`result += ...`)
3. A conditional branch on a boolean expression follows (e.g., `if (!same)`)

The generated code tests the wrong register (RAX instead of R8 where the logical not result is stored).

## Minimal Reproduction

```cpp
struct Point { int x; int y; };

template<typename T, bool v>
struct integral_constant { static constexpr bool value = v; };

template<typename T, typename U>
struct is_same : integral_constant<bool, false> {};

template<typename T>
struct is_same<T, T> : integral_constant<bool, true> {};

int main() {
    int result = 0;
    result += 10;  // Compound assignment
    
    Point p = {20, 12};
    auto [x, y] = p;  // Structured bindings
    result += x + y;
    
    // This check FAILS incorrectly
    if (!is_same<int, int>::value) {
        return 1;  // Bug: this executes when it shouldn't
    }
    
    return result;  // Should return 42
}
```

Expected: Returns 42
Actual: Returns 1

## Root Cause Analysis

### 1. The Generated Assembly Shows the Issue

```asm
39d:  sete   %r8b              # R8 = logical not result (correct)
3a1:  mov    %ebx,-0x6c(%rbp)  # Flush dirty registers
3a4:  mov    %esi,-0xc1(%rbp)  
3aa:  mov    %r8b,-0xd1(%rbp)  # R8 flushed to stack (correct)
3b1:  test   %rax,%rax         # BUG: tests RAX, not R8!
3b4:  je     3c4               # Branch based on wrong value
```

### 2. The Register Allocator State

After `handleLogicalNot`:
- R8 contains the logical not result (0 or 1)
- R8 is marked as dirty with an associated stack offset
- The register allocator maps R8 to the result temp variable's offset

After `flushAllDirtyRegisters`:
- R8's value is written to stack
- `isDirty` flag is cleared
- BUT `stackVariableOffset` mapping is NOT cleared

When `handleConditionalBranch` runs:
- It calls `tryGetStackVariableRegister(var_offset)` 
- This SHOULD return R8 (since mapping still exists)
- But `var_offset` computed here differs from what `handleLogicalNot` used!

### 3. The Offset Mismatch

The bug involves a mismatch between:
1. The offset used by `handleLogicalNot` when setting `regAlloc.set_stack_variable_offset(result_physical_reg, result_offset, size_in_bits)`
2. The offset returned by `getStackOffsetFromTempVar(temp_var)` in `handleConditionalBranch`

This can happen when:
- Variable offsets are not 8-byte aligned (structured bindings create non-aligned offsets like -76, -92)
- The `getTempVarFromOffset` check `(stackVariableOffset % 8) == 0` fails for non-aligned offsets
- This causes `flushAllDirtyRegisters` to skip flushing certain variables

### 4. IR Instruction Ordering Issue

There's also evidence of an IR instruction ordering issue where:
- IR instructions for `global_load`, `lnot`, and `br i1` are generated
- But when the IR converter processes instructions, it doesn't see `LogicalNot` or `ConditionalBranch` in the expected sequence

## Proposed Fix

### Short-term Fix: Always Flush Dirty Registers

Remove the conditional check in `flushAllDirtyRegisters` that requires `getTempVarFromOffset` to succeed:

```cpp
void flushAllDirtyRegisters()
{
    regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset, int size_in_bits)
        {
            // Always flush - don't skip based on offset alignment
            if (stackVariableOffset < variable_scopes.back().scope_stack_space) {
                variable_scopes.back().scope_stack_space = stackVariableOffset;
            }
            assert(variable_scopes.back().scope_stack_space <= stackVariableOffset && stackVariableOffset <= 0);

            emitMovToFrameSized(
                SizedRegister{reg, 64, false},
                SizedStackSlot{stackVariableOffset, size_in_bits, false}
            );
        });
}
```

### Long-term Fix: Ensure Consistent Stack Offsets

1. **Align all variable offsets to 8 bytes**: Modify `handleVariableDecl` and related functions to ensure all variables (including structured binding decompositions) are 8-byte aligned.

2. **Fix `getTempVarFromOffset`**: Update the function to handle non-aligned offsets:
```cpp
std::optional<TempVar> getTempVarFromOffset(int32_t stackVariableOffset) {
    // Search through all registered temp vars instead of computing from offset
    for (const auto& [handle, var_info] : variable_scopes.back().variables) {
        if (var_info.offset == stackVariableOffset) {
            // Return TempVar for this offset
            return TempVar(/* construct from handle or other info */);
        }
    }
    return std::nullopt;
}
```

3. **Clear register mappings after flush**: Optionally clear `stackVariableOffset` mappings in `flushAllDirtyRegisters` to force reloading from memory:
```cpp
void flushAllDirtyRegisters(Func func) {
    for (auto& reg : registers) {
        if (reg.isDirty) {
            func(reg.reg, reg.stackVariableOffset, reg.size_in_bits);
            reg.isDirty = false;
            reg.stackVariableOffset = INT_MIN;  // Clear mapping to force reload
        }
    }
}
```

4. **Investigate IR instruction ordering**: Determine why `LogicalNot` and `ConditionalBranch` instructions aren't being processed in the expected sequence and fix the IR generation or conversion pass.

## Testing

After implementing fixes, verify with:
1. The minimal reproduction case above
2. Full test suite (`./tests/run_all_tests.sh`)
3. New test case combining structured bindings, compound assignment, and template specialization checks

## Related Files

- `src/IRConverter.h`: Contains `flushAllDirtyRegisters`, `handleConditionalBranch`, `handleLogicalNot`
- `src/CodeGen.h`: IR generation for LogicalNot and ConditionalBranch
- `src/IRTypes.h`: TempVar and IrOpcode definitions

## References

- Original discovery: PR adding void_t SFINAE tests
- Test files: `tests/test_std_header_features_comprehensive_ret42.cpp`
