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
    
    // BUG: is_same<int,int>::value should be true, but the generated code
    // tests RAX (which has a stale value) instead of R8 (which has the
    // correct logical NOT result). This causes the wrong branch to execute.
    if (!is_same<int, int>::value) {
        return 1;  // Bug: this executes when it shouldn't
    }
    
    return result;  // Should return 42
}
```

Expected: Returns 42 (is_same<int,int>::value is true, so !value is false, branch not taken)
Actual: Returns 1 (wrong register tested, branch incorrectly taken)

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

### 3. The Offset Mismatch (Detailed Explanation)

The bug involves a mismatch between:
1. The offset used by `handleLogicalNot` when setting `regAlloc.set_stack_variable_offset(result_physical_reg, result_offset, size_in_bits)`
2. The offset returned by `getStackOffsetFromTempVar(temp_var)` in `handleConditionalBranch`

**Why the offsets can differ:**

When structured bindings decompose a struct, they create variables at non-8-byte-aligned offsets:
- `x` might be at offset `-76` (not divisible by 8)
- `y` might be at offset `-92` (not divisible by 8)

The `getTempVarFromOffset` function has this check:
```cpp
if (stackVariableOffset < 0 && (stackVariableOffset % 8) == 0) {
    // Only returns TempVar for 8-byte-aligned offsets
    return TempVar(var_number);
}
return std::nullopt;  // Returns nothing for non-aligned offsets!
```

When `flushAllDirtyRegisters` calls `getTempVarFromOffset` for a non-aligned offset like `-76`:
- The check `(-76 % 8) == 0` evaluates to `false` (since -76 % 8 = -4)
- `getTempVarFromOffset` returns `std::nullopt`
- The flush callback's `if (tempVarIndex.has_value())` check fails
- **The register is NOT flushed to stack!**

Later, when `handleConditionalBranch` calls `tryGetStackVariableRegister(var_offset)`:
- If the offset doesn't match what the register allocator stored, no register is found
- It falls back to using RAX and loading from memory
- But RAX still has a stale value from an earlier operation

### 4. IR Instruction Ordering Issue

There's also evidence of an IR instruction ordering issue where:
- IR instructions for `global_load`, `lnot`, and `br i1` are generated
- But when the IR converter processes instructions, it doesn't see `LogicalNot` or `ConditionalBranch` in the expected sequence

## Proposed Fix

### Short-term Fix: Always Flush Dirty Registers

The current code in `flushAllDirtyRegisters` (IRConverter.h ~line 4796):

```cpp
// CURRENT CODE (BUGGY)
void flushAllDirtyRegisters()
{
    regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset, int size_in_bits)
        {
            auto tempVarIndex = getTempVarFromOffset(stackVariableOffset);

            if (tempVarIndex.has_value()) {  // <-- BUG: This check fails for non-aligned offsets!
                // ... flush logic ...
                emitMovToFrameSized(...);
            }
            // If tempVarIndex is nullopt, the register is NOT flushed!
        });
}
```

**Proposed fix** - Remove the conditional check and always flush:

```cpp
// FIXED CODE
void flushAllDirtyRegisters()
{
    regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset, int size_in_bits)
        {
            // Always flush dirty registers to stack, regardless of offset alignment
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

3. **Clear register mappings after flush**: Optionally clear `stackVariableOffset` mappings in `flushAllDirtyRegisters` to force reloading from memory. This uses `INT_MIN` as the sentinel value because that's the existing convention in the codebase (see `AllocatedRegister::stackVariableOffset = INT_MIN` initialization):
```cpp
void flushAllDirtyRegisters(Func func) {
    for (auto& reg : registers) {
        if (reg.isDirty) {
            func(reg.reg, reg.stackVariableOffset, reg.size_in_bits);
            reg.isDirty = false;
            reg.stackVariableOffset = INT_MIN;  // Clear mapping (INT_MIN is the existing sentinel)
        }
    }
}
```

4. ~~**Investigate IR instruction ordering**: Determine why `LogicalNot` and `ConditionalBranch` instructions aren't being processed in the expected sequence and fix the IR generation or conversion pass.~~ (Not applicable - the real issue was size tracking)

## Solution Implemented (December 2025)

### Actual Root Cause

The bug was **NOT** about non-8-byte-aligned offsets from structured bindings. The real issue was that `UnaryOp` results (like `LogicalNot`) were not registering their result size in the `temp_var_sizes_` map during stack space calculation.

When `handleConditionalBranch` tried to load the condition value, it:
1. Looked up the size in `temp_var_sizes_`
2. Found nothing (because UnaryOp didn't register)
3. Defaulted to 32 bits
4. Loaded 32 bits from memory where only 8 bits were written
5. Read the 8-bit result PLUS 24 bits of uninitialized stack garbage
6. Made incorrect branching decisions based on the garbage

### Fixes Applied

1. **Added UnaryOp size tracking** in `calculateFunctionStackSpace()`:
   ```cpp
   // Try UnaryOp (logical not, bitwise not, negate)
   else if (const UnaryOp* unary_op = std::any_cast<UnaryOp>(&instruction.getTypedPayload())) {
       temp_var_sizes_[StringTable::getOrInternStringHandle(unary_op->result.name())] = unary_op->value.size_in_bits;
       handled_by_typed_payload = true;
   }
   ```

2. **Removed conditional check in flushAllDirtyRegisters()**: Now always flushes dirty registers regardless of offset alignment, ensuring proper register-to-memory synchronization.

3. **Clear register mappings after flush**: Set `stackVariableOffset = INT_MIN` after flushing to prevent stale register lookups.

### Test Results

All 797 tests in the test suite pass, including:
- `tests/test_register_flush_bug_minimal.cpp` - Returns 42 ✓
- `tests/test_register_flush_simple2.cpp` - Logical NOT works correctly ✓
- `tests/test_std_header_features_comprehensive_ret42.cpp` - Comprehensive test passes ✓

## Testing

After implementing fixes, verify with:
1. The minimal reproduction case above ✓
2. Full test suite (`./tests/run_all_tests.sh`) ✓
3. New test case combining structured bindings, compound assignment, and template specialization checks ✓

## Related Files

- `src/IRConverter.h`: Contains `flushAllDirtyRegisters`, `handleConditionalBranch`, `handleLogicalNot`, `calculateFunctionStackSpace`
- `src/CodeGen.h`: IR generation for LogicalNot and ConditionalBranch
- `src/IRTypes.h`: TempVar and IrOpcode definitions

## References

- Original discovery: PR adding void_t SFINAE tests
- Test files: `tests/test_std_header_features_comprehensive_ret42.cpp`
- Fix commit: December 2025 - Added UnaryOp size tracking
