# Register Flush Bug - Solution Summary

## Problem
The register flush bug caused incorrect code generation when using logical NOT operations (`!`) before conditional branches. The symptom was that the wrong branch would be taken even when the condition was correct.

## Root Cause
The bug was caused by missing size information for UnaryOp results (like LogicalNot). When the conditional branch code tried to load the condition value from the stack, it:
1. Looked up the size in `temp_var_sizes_` map
2. Found nothing (UnaryOp didn't register its result size)
3. Defaulted to 32 bits instead of the correct 8 bits
4. Loaded 32 bits from memory where only 8 bits were written
5. Read the correct 8-bit result PLUS 24 bits of uninitialized stack garbage
6. Made incorrect branching decisions based on the garbage values

## Solution
Added UnaryOp case to the `calculateFunctionStackSpace()` function to register the result size:

```cpp
// Try UnaryOp (logical not, bitwise not, negate)
else if (const UnaryOp* unary_op = std::any_cast<UnaryOp>(&instruction.getTypedPayload())) {
    temp_var_sizes_[StringTable::getOrInternStringHandle(unary_op->result.name())] = unary_op->value.size_in_bits;
    handled_by_typed_payload = true;
}
```

Additionally implemented two defensive improvements:
1. Always flush dirty registers in `flushAllDirtyRegisters()` (removed conditional check)
2. Clear register mappings after flush by setting `stackVariableOffset = INT_MIN`

## Test Results
- All 797 tests in the test suite pass
- Minimal reproduction case now works correctly
- No regressions introduced

## Files Modified
- `src/IRConverter.h`: Added UnaryOp size tracking, improved register flushing
- `docs/REGISTER_FLUSH_BUG.md`: Updated with solution details
- `tests/test_register_flush_*.cpp`: Added test cases for regression testing
