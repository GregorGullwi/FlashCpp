# RVO/NRVO Implementation

## Status: Detection Infrastructure Complete ✅

**Date**: December 2025  
**Total Lines Added**: ~90 lines  
**Test Status**: All 657 tests passing (0 failures, 0 regressions)

---

## Overview

This document describes the implementation of Return Value Optimization (RVO) and Named Return Value Optimization (NRVO) detection infrastructure in FlashCpp. The implementation leverages the existing value category tracking system to detect copy elision opportunities as mandated by C++17.

### What is RVO/NRVO?

- **RVO (Return Value Optimization)**: Eliminates unnecessary copying when returning a temporary object (prvalue) from a function by constructing the object directly in the caller's storage location.
  
- **NRVO (Named Return Value Optimization)**: Similar to RVO, but for named local variables that are returned from a function.

- **C++17 Mandate**: C++17 made copy elision mandatory when a prvalue is used to initialize an object of the same type. This includes function returns (`return Point(3, 4)`), direct initialization (`Point p = Point(3, 4)`), and temporary materialization.

---

## Current Implementation

### Phase 1: Detection Infrastructure (Complete)

The implementation adds detection and metadata tracking for RVO opportunities without implementing the actual copy elision optimization. This provides:

1. **Metadata Fields** (in `src/IRTypes.h`):
   - `is_return_value`: Tracks if a TempVar is being returned from a function
   - `eligible_for_rvo`: Marks prvalues that can be constructed directly in return slot
   - `eligible_for_nrvo`: Marks named variables eligible for NRVO

2. **Helper Functions**:
   - `TempVarMetadata::makeRVOEligiblePRValue()`: Creates metadata for RVO-eligible prvalues
   - `TempVarMetadata::makeNRVOCandidate()`: Creates metadata for NRVO candidates
   - `isTempVarRVOEligible()`: Queries if a TempVar is RVO-eligible
   - `isTempVarNRVOEligible()`: Queries if a TempVar is NRVO-eligible
   - `markTempVarAsReturnValue()`: Marks a TempVar as being returned

3. **Constructor Call Marking** (in `src/CodeGen.h`):
   ```cpp
   // Mark constructor call results as RVO-eligible
   setTempVarMetadata(ret_var, TempVarMetadata::makeRVOEligiblePRValue());
   ```

4. **Return Statement Detection** (in `src/CodeGen.h`):
   ```cpp
   // Detect RVO opportunities in return statements
   if (isTempVarRVOEligible(return_temp)) {
       FLASH_LOG_FORMAT(Codegen, Debug,
           "RVO opportunity detected: returning prvalue {} (constructor call result)",
           return_temp.name());
   }
   ```

### Example Detection Output

For code like:
```cpp
Point makePoint() {
    return Point(3, 4);
}
```

The compiler logs:
```
Marked constructor call result temp_1 as RVO-eligible prvalue
RVO opportunity detected: returning prvalue temp_1 (constructor call result)
```

---

## Architecture Integration

The RVO detection leverages the existing value category infrastructure:

```
Parser → CodeGen → Mark constructor calls as RVO-eligible prvalues
                 ↓
         TempVar + TempVarMetadata (RVO flags)
                 ↓
         Return statement → Detect RVO opportunity
                 ↓
         IRConverter → (Future: use metadata for optimization)
```

**Key Design Principles**:
- Optional metadata (only RVO-eligible temps are marked)
- 100% backward compatible (no behavior changes)
- Metadata travels with TempVars
- Minimal code changes (~90 lines total)

---

## What's Missing for Full Implementation

To implement actual copy elision (not just detection), the following work is required:

### System V AMD64 ABI Hidden Return Parameter

On Linux/Unix (System V AMD64 ABI), when a function returns a struct by value:

1. **Caller's Responsibility**:
   - Allocate space for return value on stack or use existing variable's location
   - Pass pointer to return space as **hidden first parameter** in `RDI`
   - Shift other parameters: 1st arg → RSI, 2nd → RDX, etc.

2. **Callee's Responsibility**:
   - Construct object directly at address in `RDI`
   - Return normally (no struct copying needed)

### Required Changes

1. **Function Call Site** (`handleFunctionCall` in `IRConverter.h`):
   ```cpp
   // For struct return types:
   // 1. Allocate return slot or use existing variable location
   // 2. Pass return slot pointer as first parameter (RDI on Linux)
   // 3. Shift other parameters accordingly
   ```

2. **Function Declaration** (`handleFunctionDecl` in `IRConverter.h`):
   ```cpp
   // For struct return types:
   // 1. Expect hidden first parameter in RDI
   // 2. Use this address for all construction operations
   // 3. Propagate to constructor calls
   ```

3. **Constructor Calls** (`handleConstructorCall` in `IRConverter.h`):
   ```cpp
   // When RVO is active:
   // 1. Construct directly at return slot address (from hidden parameter)
   // 2. Skip copying to local temporary
   ```

4. **Return Statements** (`handleReturn` in `IRConverter.h`):
   ```cpp
   // When RVO is active:
   // 1. Object already constructed at return slot
   // 2. Just emit epilogue and return
   // 3. No need to copy struct to RAX
   ```

### Estimated Effort

- **Detection infrastructure**: Complete (~90 lines)
- **Hidden return parameter support**: Medium (~200-300 lines)
- **Constructor integration**: Small (~50 lines)
- **Testing and validation**: Medium

---

## Testing

### Current Tests

All existing 657 tests pass with RVO detection:
- No behavior changes
- No regressions
- RVO opportunities are logged but not acted upon

### Future Tests

Created test files for full RVO/NRVO implementation:
- `tests/test_rvo_basic.cpp`: Simple prvalue return
- `tests/test_nrvo_basic.cpp`: Named return value
- `tests/test_rvo_comprehensive.cpp`: Multiple scenarios

---

## Benefits of Current Implementation

Even without full copy elision, the detection infrastructure provides:

1. **Foundation for Optimization**: Metadata and detection logic in place
2. **Documentation**: RVO opportunities are logged for analysis
3. **No Regressions**: Zero impact on existing functionality
4. **Incremental Development**: Can implement full optimization later

---

## Next Steps

To complete RVO/NRVO implementation:

1. Implement hidden return parameter support (System V AMD64 ABI)
2. Integrate return slot propagation to constructor calls
3. Update return statement handling to skip copies for RVO-eligible returns
4. Test with comprehensive RVO/NRVO test suite
5. Add Windows x64 ABI support (different calling convention)

---

## References

- C++17 Standard: [class.copy.elision]
- System V AMD64 ABI: Section 3.2.3 "Parameter Passing"
- Value Category Infrastructure: `docs/STRUCT_ARRAY_MEMBER_ACCESS_FIX.md`
- Value Category Analysis: `docs/VALUE_CATEGORY_REFACTORING_ANALYSIS.md`

---

## Key Files

- `src/IRTypes.h`: RVO/NRVO metadata fields and helpers (~50 lines added)
- `src/CodeGen.h`: Constructor marking and return detection (~40 lines added)
- `tests/test_rvo_*.cpp`: Test cases for future validation

---

## Impact

**Added**: ~90 lines (infrastructure)  
**Removed**: 0 lines (backward compatible)  
**Net**: +90 lines for RVO/NRVO detection foundation

**Benefits**:
- Detection infrastructure complete
- Foundation for mandatory C++17 copy elision
- No behavior changes or regressions
- Ready for future optimization implementation
