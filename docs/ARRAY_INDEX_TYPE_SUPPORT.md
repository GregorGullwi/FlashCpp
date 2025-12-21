# Array Index Type Support Implementation Plan

## Problem Statement

Currently, the `ComputeAddressOp::ArrayIndex` struct assumes all array indices are 32-bit signed integers. This is a limitation because:

1. **C++ Standard Support**: The C++ standard allows various integer types as array indices:
   - `int`, `long`, `long long` (signed variants)
   - `unsigned int`, `unsigned long`, `unsigned long long`, `size_t` (unsigned variants)
   - `ptrdiff_t` (typically signed, used for pointer arithmetic)

2. **Current Implementation Gap**:
   - `ArrayIndex` only stores: `index` (value) and `element_size_bits`
   - Missing: `index_type` and `index_size_bits`
   - Code generation assumes 32-bit signed int for all variable indices

3. **Impact**:
   - Incorrect code for 64-bit indices (e.g., `size_t` on 64-bit systems)
   - Incorrect code for unsigned vs signed indices (affects sign extension)
   - May truncate large index values

## What's Missing

### In IRTypes.h - ArrayIndex struct:
```cpp
struct ArrayIndex {
    std::variant<unsigned long long, TempVar, StringHandle> index;
    int element_size_bits;  // ✓ Has this
    // MISSING:
    Type index_type;         // Type of the index (Int, UnsignedInt, Long, UnsignedLong, etc.)
    int index_size_bits;     // Size of the index in bits (32, 64, etc.)
};
```

### In CodeGen.h - analyzeAddressExpression:
Currently when creating ArrayIndex, we only use `index_operands[2]` (the value).
We need to also capture:
- `index_operands[0]` → `Type` (index type)
- `index_operands[1]` → `int` (index size in bits)

### In IRConverter.h - handleComputeAddress:
Currently hardcoded to:
```cpp
emitMovFromFrameSized(
    SizedRegister{X64Register::RCX, 64, false},
    SizedStackSlot{static_cast<int32_t>(index_offset), 32, true}  // ← Hardcoded 32-bit signed
);
```

Should use the actual index type and size from ArrayIndex.

## Implementation Plan

### Phase 1: Extend ArrayIndex Structure
**File**: `src/IRTypes.h`

Add type and size fields to `ComputeAddressOp::ArrayIndex`:
```cpp
struct ArrayIndex {
    std::variant<unsigned long long, TempVar, StringHandle> index;
    int element_size_bits;     // Size of array element
    Type index_type;           // Type of the index (for proper sign extension)
    int index_size_bits;       // Size of the index in bits
};
```

**Update debug printing** in `IRTypes.h` to show index type/size.

### Phase 2: Capture Index Type Information
**File**: `src/CodeGen.h` - `analyzeAddressExpression()`

When creating ArrayIndex objects:
```cpp
// Add this array index
ComputeAddressOp::ArrayIndex arr_idx;
arr_idx.element_size_bits = element_size_bits;

// Capture index type information
Type index_type = std::get<Type>(index_operands[0]);
int index_size_bits = std::get<int>(index_operands[1]);
arr_idx.index_type = index_type;
arr_idx.index_size_bits = index_size_bits;

// Set index value (existing code)
if (std::holds_alternative<unsigned long long>(index_operands[2])) {
    arr_idx.index = std::get<unsigned long long>(index_operands[2]);
} // ... etc
```

### Phase 3: Use Type Information in Code Generation
**File**: `src/IRConverter.h` - `handleComputeAddress()`

Replace hardcoded assumptions with actual type info:
```cpp
// Load index into RCX with proper size and sign extension
bool is_signed = isSignedType(arr_idx.index_type);
emitMovFromFrameSized(
    SizedRegister{X64Register::RCX, 64, false},
    SizedStackSlot{
        static_cast<int32_t>(index_offset), 
        arr_idx.index_size_bits,  // Use actual size instead of hardcoded 32
        is_signed                 // Use actual signedness instead of hardcoded true
    }
);
```

### Phase 4: Testing
Create test cases for:
1. **Different index types**:
   - `int` index (32-bit signed) - existing behavior
   - `size_t` index (64-bit unsigned)
   - `long long` index (64-bit signed)
   - `unsigned int` index (32-bit unsigned)

2. **Edge cases**:
   - Large unsigned indices (test no incorrect sign extension)
   - 64-bit indices on arrays
   - Mixed index types in nested subscripts

Test files to create:
- `tests/test_addressof_size_t_index.cpp` - size_t index
- `tests/test_addressof_longlong_index.cpp` - long long index
- `tests/test_addressof_unsigned_index.cpp` - unsigned int index

### Phase 5: Verify No Regressions
- Run full test suite (665 tests)
- Verify existing tests still pass
- Ensure backward compatibility with existing code

## Benefits

1. **Correctness**: Proper handling of all C++ integer types as array indices
2. **Standard Compliance**: Full C++ standard support for array indexing
3. **64-bit Support**: Correct behavior on 64-bit platforms with size_t
4. **Sign Extension**: Proper sign/zero extension based on actual type

## Risks & Mitigation

**Risk**: Breaking existing code that depends on 32-bit assumption
**Mitigation**: This is backward compatible - 32-bit signed int is still the common case, now properly represented

**Risk**: Performance impact from checking type
**Mitigation**: Type checking is compile-time, no runtime overhead

**Risk**: Increased IR size
**Mitigation**: Only 2 additional fields per ArrayIndex, negligible impact

## Timeline

- Phase 1 (IR Structure): 30 minutes
- Phase 2 (Capture Info): 30 minutes  
- Phase 3 (Code Gen): 1 hour
- Phase 4 (Testing): 1 hour
- Phase 5 (Validation): 30 minutes

**Total**: ~3-4 hours

## Success Criteria

✅ ArrayIndex stores complete type information
✅ Code generation uses actual index type/size
✅ All existing 665 tests pass
✅ New tests verify different index types work correctly
✅ No hardcoded assumptions about index types remain
