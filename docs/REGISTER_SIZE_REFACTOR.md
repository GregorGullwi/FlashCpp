# Register Size Refactor Plan

## Problem Summary

FlashCpp has pervasive issues with MOV operation sizes in code generation:

1. **Wrong-sized MOV operations**: The `emitMovFromFrame()` function always emits 64-bit loads regardless of the actual operand size
2. **Missing sign extensions**: Signed 8/16/32-bit values are loaded with MOVZX (zero-extend) instead of MOVSX (sign-extend)
3. **Missing REX.W prefixes**: Some 64-bit operations lack the required REX.W prefix
4. **No size-aware register abstraction**: The `X64Register` enum represents only register identity (RAX, RCX, etc.) without operational size (AL/AX/EAX/RAX)

### Evidence of Bugs

Existing tests have **incorrect expected values** baked in due to these bugs:

| Test File | Expected | Should Be | Notes |
|-----------|----------|-----------|-------|
| `test_movzx8.cpp` | 0 | 100 | `char a = 100` returns garbage |
| `test_movzx16.cpp` | 0 | 1000 | `short a = 1000` returns garbage |
| `test_char_only.cpp` | -112 | 5 | `char c = 5` returns wrong value |
| `test_short_only.cpp` | -23463 | 10 | `short s = 10` returns wrong value |
| `test_two_members.cpp` | -32855 | 15 | `c + s = 5 + 10` returns garbage |
| `test_direct_add.cpp` | -9 | 15 | Same calculation, wrong result |

---

## Current Architecture

### Register Representation (`src/AstToIr.cpp`, line ~211)

```cpp
enum class X64Register : uint8_t {
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,  // 0-7
    R8, R9, R10, R11, R12, R13, R14, R15,    // 8-15
    XMM0-XMM15,                              // 16-31
    Count                                     // 32
};
```

**Problem**: No size encoded—cannot distinguish AL/AX/EAX/RAX operations.

### RegisterAllocator Tracking (`src/AstToIr.cpp`, line ~1470)

```cpp
struct AllocatedRegister {
    X64Register reg = X64Register::Count;
    bool isAllocated = false;
    bool isDirty = false;
    int32_t stackVariableOffset = INT_MIN;
    int size_in_bits = 0;  // Size IS tracked here
};
```

**Observation**: Size tracking exists in `AllocatedRegister` but is **not consistently used** when emitting MOV instructions.

### TypedValue Structure (`src/AstToIr.cpp`, line ~511)

```cpp
struct TypedValue {
    Type type = Type::Void;
    int size_in_bits = 0;      // Size IS available
    IrValue value;
    bool is_reference = false;
    // Missing: bool is_signed;  // NEEDED for MOVSX vs MOVZX
};
```

---

## Problematic Functions

### `emitMovFromFrame()` - Always 64-bit (CRITICAL BUG)

```cpp
// Line ~3432
void emitMovFromFrame(X64Register destinationRegister, int32_t offset) {
    auto opcodes = generateMovFromFrameBySize(destinationRegister, offset, 64);  // ALWAYS 64-bit!
    textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), 
                           opcodes.op_codes.begin() + opcodes.size_in_bytes);
}
```

**34 call sites**, approximately 20 of which are incorrect for non-64-bit values.

### `loadOperandIntoRegister()` - Always 64-bit

```cpp
// Line ~6848
X64Register loadOperandIntoRegister(const IrInstruction& instruction, size_t operand_index) {
    // ...
    auto mov_opcodes = generatePtrMovFromFrame(reg, stack_addr);  // ALWAYS 64-bit!
    // ...
}
```

### `loadTypedValueIntoRegister()` - Correct (Reference Implementation)

```cpp
// Line ~6892
X64Register loadTypedValueIntoRegister(const TypedValue& typed_value) {
    // ...
    auto mov_opcodes = generateMovFromFrameBySize(reg, stack_addr, typed_value.size_in_bits);  // ✅ Uses size
    // ...
}
```

---

## Proposed Solution

### Phase 1: Introduce `SizedRegister` Struct

Add near the `X64Register` enum:

```cpp
/// Bundles a register with its operational size and signedness.
/// Use this instead of bare X64Register when emitting MOV instructions.
struct SizedRegister {
    X64Register reg;
    int size_in_bits;    // 8, 16, 32, or 64
    bool is_signed;      // true = use MOVSX, false = use MOVZX for loads < 64-bit
    
    SizedRegister(X64Register r, int size, bool sign = false)
        : reg(r), size_in_bits(size), is_signed(sign) {}
    
    // Convenience constructors
    static SizedRegister ptr(X64Register r) { return {r, 64, false}; }
    static SizedRegister i64(X64Register r) { return {r, 64, true}; }
    static SizedRegister i32(X64Register r) { return {r, 32, true}; }
    static SizedRegister i16(X64Register r) { return {r, 16, true}; }
    static SizedRegister i8(X64Register r) { return {r, 8, true}; }
    static SizedRegister u64(X64Register r) { return {r, 64, false}; }
    static SizedRegister u32(X64Register r) { return {r, 32, false}; }
    static SizedRegister u16(X64Register r) { return {r, 16, false}; }
    static SizedRegister u8(X64Register r) { return {r, 8, false}; }
};
```

### Phase 2: Add Signedness to `TypedValue`

```cpp
struct TypedValue {
    Type type = Type::Void;
    int size_in_bits = 0;
    bool is_signed = false;  // NEW: Derive from Type during construction
    IrValue value;
    bool is_reference = false;
};
```

Helper function to determine signedness from `Type`:

```cpp
bool isSignedType(Type t) {
    switch (t) {
        case Type::Char:      // char is signed by default in MSVC
        case Type::Short:
        case Type::Int:
        case Type::Long:
        case Type::LongLong:
            return true;
        default:
            return false;
    }
}
```

### Phase 3: Add Sign-Extending Frame Load Functions

```cpp
// In IRConverter.h - add alongside existing generateMovFromFrame* functions

/// Load 8-bit signed value from frame with sign extension to 32/64-bit
OpCodeWithSize generateMovsxFromFrame8(X64Register dest, int32_t offset, int target_bits);

/// Load 16-bit signed value from frame with sign extension to 32/64-bit  
OpCodeWithSize generateMovsxFromFrame16(X64Register dest, int32_t offset, int target_bits);

/// Load 32-bit signed value from frame with sign extension to 64-bit
OpCodeWithSize generateMovsxdFromFrame32(X64Register dest, int32_t offset);
```

Implementation (x86-64 encoding):

```cpp
// MOVSX r32/r64, byte ptr [rbp+offset]: REX? 0F BE /r
// MOVSX r32/r64, word ptr [rbp+offset]: REX? 0F BF /r  
// MOVSXD r64, dword ptr [rbp+offset]: REX.W 63 /r
```

### Phase 4: Create Size-Aware `emitMovFromFrameSized()`

```cpp
void emitMovFromFrameSized(SizedRegister dest, int32_t offset) {
    OpCodeWithSize opcodes;
    
    if (dest.size_in_bits == 64) {
        opcodes = generateMovFromFrame64(dest.reg, offset);
    } else if (dest.is_signed) {
        // Sign-extend smaller values
        switch (dest.size_in_bits) {
            case 8:  opcodes = generateMovsxFromFrame8(dest.reg, offset, 64); break;
            case 16: opcodes = generateMovsxFromFrame16(dest.reg, offset, 64); break;
            case 32: opcodes = generateMovsxdFromFrame32(dest.reg, offset); break;
        }
    } else {
        // Zero-extend smaller values (existing behavior)
        opcodes = generateMovFromFrameBySize(dest.reg, offset, dest.size_in_bits);
    }
    
    textSectionData.insert(textSectionData.end(), 
                           opcodes.op_codes.begin(), 
                           opcodes.op_codes.begin() + opcodes.size_in_bytes);
}
```

### Phase 5: Fix Call Sites Incrementally

Replace each `emitMovFromFrame(reg, offset)` with `emitMovFromFrameSized(SizedRegister{...}, offset)`:

**Example transformations:**

```cpp
// Before (always 64-bit):
emitMovFromFrame(target_reg, var_offset);

// After (size-aware):
emitMovFromFrameSized(SizedRegister{target_reg, arg.size_in_bits, isSignedType(arg.type)}, var_offset);

// Or for pointers (always 64-bit, always unsigned):
emitMovFromFrameSized(SizedRegister::ptr(target_reg), var_offset);
```

### Phase 6: Update `loadOperandIntoRegister()`

```cpp
X64Register loadOperandIntoRegister(const IrInstruction& inst, size_t idx, int size_bits, bool is_signed) {
    // ... existing register allocation logic ...
    
    if (stack_addr != INT_MIN) {
        emitMovFromFrameSized(SizedRegister{reg, size_bits, is_signed}, stack_addr);
    }
    
    return reg;
}
```

---

## Call Sites to Fix

All 34 `emitMovFromFrame()` calls in `src/AstToIr.cpp`:

| Line | Context | Action |
|------|---------|--------|
| 2980 | Binary op RHS loading | Use operand size |
| 4033-4045 | Call argument loading | Use argument type size |
| 4289 | Call argument TempVar | Use argument type size |
| 4334, 4370, 4376 | Stack argument loading | Use argument type size |
| 4512 | Constructor `this` pointer | Keep 64-bit (pointer) |
| 4610, 4624 | Constructor params | Use parameter type size |
| 4729 | Virtual call vptr load | Keep 64-bit (pointer) |
| 4821, 4832 | Heap array count | Use size_t (64-bit) |
| 4903, 4945 | Heap free pointer | Keep 64-bit (pointer) |
| 4969, 4980 | Placement new address | Keep 64-bit (pointer) |
| 5101 | memcpy source | Keep 64-bit (pointer) |
| 7949 | Function pointer assignment | Keep 64-bit (pointer) |
| 7992, 8009 | Struct assignment | Use struct member sizes |
| 8059, 8078 | Assignment RHS | Use RHS type size |
| 8431 | Array index loading | Use index type size |
| 8517 | Value loading | Use value type size |
| 8868 | Object base for member store | Keep 64-bit (pointer) |
| 9128, 9137 | Pointer loading | Keep 64-bit (pointer) |
| 9400, 9406 | Function pointer | Keep 64-bit (pointer) |
| 9429, 9440 | Indirect call args | Use argument type size |

---

## Testing Strategy

### Before Refactoring

1. Run existing tests to establish baseline (current buggy behavior)
2. Note which tests have incorrect expected values

### During Refactoring

1. Fix one call site category at a time (e.g., all function call argument loads)
2. After each fix, run tests to catch any regressions
3. Update expected values in `tests/test_reference_files.ps1` as bugs are fixed

### After Refactoring

Update these test expected values to correct results:

```powershell
# In tests/test_reference_files.ps1
"test_movzx8.cpp" = 100        # Was: 0
"test_movzx16.cpp" = 1000      # Was: 0  
"test_char_only.cpp" = 5       # Was: -112
"test_short_only.cpp" = 10     # Was: -23463
"test_two_members.cpp" = 15    # Was: -32855
"test_direct_add.cpp" = 15     # Was: -9
"integer_promotions.cpp" = 78  # Verify this is correct
```

---

## Migration Checklist

- [ ] Add `SizedRegister` struct
- [ ] Add `is_signed` field to `TypedValue`
- [ ] Implement `generateMovsxFromFrame*()` functions in `IRConverter.h`
- [ ] Add `emitMovFromFrameSized()` wrapper function
- [ ] Fix function call argument loading (lines 4033-4376)
- [ ] Fix assignment RHS loading (lines 8059, 8078)
- [ ] Fix array index loading (line 8431)
- [ ] Fix binary operation operand loading (line 2980)
- [ ] Update `loadOperandIntoRegister()` to accept size parameter
- [ ] Verify pointer loads remain 64-bit
- [ ] Update test expected values
- [ ] Run full test suite
- [ ] Remove deprecated `emitMovFromFrame()` or mark as `[[deprecated]]`

---

## References

- x86-64 MOV instruction encoding: Intel SDM Vol. 2, Chapter 4
- MOVSX/MOVSXD encoding: `REX.W 0F BE /r` (byte), `REX.W 0F BF /r` (word), `REX.W 63 /r` (dword)
- Windows x64 calling convention: Microsoft Docs
- Existing size-aware function: `generateMovFromFrameBySize()` in `IRConverter.h`
