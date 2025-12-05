# TempVar Stack Allocation - Known Issues and Proposed Improvements

## Current State (December 2025)

### Problem Description

The current TempVar stack allocation has several issues:

1. **Fixed 8-byte assumption**: All TempVars are allocated 8 bytes regardless of actual size
   - 32-bit ints waste 4 bytes per TempVar
   - Complex types might need more space

2. **Scanning-based discovery**: `calculateFunctionStackSpace()` scans all IR instructions to find TempVars
   - Fragile: easy to miss new instruction types that produce TempVars
   - Inefficient: O(n) scan of all instructions per function
   - Must be kept in sync with all TempVar-producing instructions

3. **Dual-path stack calculation**: 
   - `calculateFunctionStackSpace()` pre-calculates temp var space
   - `getStackOffsetFromTempVar()` dynamically allocates using formula
   - Stack is patched at function end using `max_temp_var_index_`
   - These can get out of sync

### Current Workaround

1. **Dynamic scope extension**: `getStackOffsetFromTempVar()` extends `scope_stack_space` 
   when a TempVar offset exceeds the pre-calculated space. This ensures assertions remain 
   valid even when more TempVars are allocated at runtime than were pre-counted.

2. **8-byte assumption**: All TempVars are allocated 8 bytes (the largest common size for x64).
   This wastes space but ensures correctness.

## Proposed Solution

### Option 1: Track TempVar sizes during IR generation

Add a `registerTempVar(TempVar var, int size_in_bits)` method to CodeGen that:
1. Records each TempVar and its size in a per-function map
2. Computes total stack space needed
3. Stores this in `FunctionDeclOp::temp_var_stack_bytes`

**Pros**: 
- Accurate sizing
- No scanning needed in IRConverter
- Single source of truth

**Cons**:
- Requires changes to many places in CodeGen.h where TempVars are created
- Need to pass size information to each var_counter.next() call

### Option 2: Typed TempVar

Modify `TempVar` class to include type/size information:
```cpp
class TempVar {
    int var_number;
    Type type;
    int size_in_bits;
};
```

**Pros**:
- Type information travels with the TempVar
- Easier debugging

**Cons**:
- Increases TempVar size (currently 4 bytes, would become 12+)
- Breaking change to many interfaces

### Option 3: StoreAlloc IR instruction

Add a new IR instruction that explicitly allocates stack space:
```cpp
struct AllocStackOp {
    TempVar result;
    int size_in_bits;
};
// IR: %3 = alloc_stack 32  ; allocate 32-bit temp
```

**Pros**:
- Explicit, self-documenting IR
- Easy to sum up stack requirements
- Matches how LLVM handles allocas

**Cons**:
- More IR instructions
- Requires changes to all TempVar creation sites

## Recommended Approach

**Short term**: Keep current 8-byte assumption but ensure all TempVar-producing 
instructions are covered in `calculateFunctionStackSpace()`.

**Medium term**: Implement Option 1 - track TempVar sizes during IR generation
and store total in `FunctionDeclOp`. This is the least invasive change that 
provides accurate sizing.

**Long term**: Consider Option 3 for cleaner IR semantics, but this is a larger
refactoring effort.

## Instructions Currently Tracked for TempVars

In `calculateFunctionStackSpace()` (IRConverter.h), the following produce TempVars:
- `BinaryOp` - arithmetic, comparisons, logic
- `CallOp` - function call results
- `ArrayAccessOp` - array element loads
- `ArrayElementAddressOp` - address of array element (always 64-bit)
- `AssignmentOp` - assignments to TempVars (for materializing literals)
- `AddressOfOp` - address-of operator results (always 64-bit)
- `DereferenceOp` - dereference operator results

### Missing (TODO)
- `UnaryOp` - negate, logical not, bitwise not
- `CastOp` - type casts
- `VirtualCallOp` - virtual function call results
- `MemberAccessOp` - member access results
- Any other instruction types that produce TempVar results

## Related Files
- `src/IRTypes.h` - TempVar class, FunctionDeclOp struct
- `src/IRConverter.h` - calculateFunctionStackSpace(), getStackOffsetFromTempVar()
- `src/CodeGen.h` - var_counter, TempVar creation
