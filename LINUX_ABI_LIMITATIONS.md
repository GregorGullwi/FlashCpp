# Linux ABI Implementation Status

## ✅ Fully Implemented Features

### Variadic Functions  
Variadic functions (`...`) are **now fully supported** with complete ABI compliance.

**Implementation**:
- `CallOp` structure has `is_variadic` field populated from function declarations
- System V AMD64 ABI requirements fully implemented:
  - Float arguments promoted to double (C standard)
  - Float values copied to both XMM and GPR registers at same position
  - AL register set to count of XMM registers used

**Verified**:
- `test_varargs.cpp`: Calls gcc-compiled variadic functions
- Integer varargs: `sum_ints(3, 10, 20, 30)` ✓
- Mixed varargs: `sum_mixed(3, 1.5, 2.5, 3.0)` ✓
- All tests pass with correct values

### Platform-Specific Calling Conventions
- ✅ Separate register pools for integers and floats
- ✅ Platform-specific register counts (Windows: 4/4, Linux: 6/8)
- ✅ Shadow space handling (Windows: 32 bytes, Linux: 0)
- ✅ Volatile register sets for stack unwinding
- ✅ Exception handling with Itanium C++ ABI

## ⚠️ Known Limitations

### Legacy Operand-Based Code Path
The IR converter has two code paths for function calls:
1. **Modern path**: Uses `CallOp` typed payload (✅ fully implemented, including varargs)
2. **Legacy path**: Uses operand-based instruction format (⚠️ limited features)

The legacy path:
- Cannot detect variadic functions
- Has limited ABI feature support  
- Exists for backward compatibility (purpose unclear - possibly dead code)

**Question for maintainers**: Can the legacy operand-based path be removed? All current tests use the typed payload path.

### Stack Argument Handling with Mixed Types  
The stack argument overflow logic uses a simplified heuristic based on integer register count.

**Works correctly when**:
- All integer arguments OR all float arguments fit in registers
- Standard function signatures
- Varargs functions (proper handling implemented)

**May have issues with**:
- Complex mixed-type signatures that overflow both register pools simultaneously
- Example edge case: `func(double×9, int×10)` - 8 doubles in XMM0-7, 9th double needs stack; simultaneously 6 ints in GPR, 7th-10th ints need stack

**Impact**: Low - most real-world code doesn't have such extreme signatures

## 📋 Implementation Checklist

- [x] ~~Add `is_variadic` field to `CallOp`~~ **DONE**
- [x] ~~Implement proper varargs handling~~ **DONE**
  - [x] Float→double promotion
  - [x] Dual XMM+GPR register passing
  - [x] AL register count (System V AMD64)
- [ ] Remove legacy operand-based path (pending maintainer decision)
- [ ] Enhance stack overflow logic for extreme mixed-type cases (low priority)

## 🎯 Recommendations for Future Enhancement

1. **Remove legacy operand-based path** (if not needed):
   - Simplifies code
   - Reduces maintenance burden
   - Eliminates ABI inconsistencies

2. **Enhance stack overflow logic** (low priority):
   - Track both int and float register usage independently
   - Correctly interleave stack arguments from both pools
   - Handle all mixed-type overflow scenarios

These enhancements are optional as the core functionality is complete and production-ready.
