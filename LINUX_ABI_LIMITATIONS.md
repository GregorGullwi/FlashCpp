# Known Limitations - Linux ABI Implementation

## Varargs Functions  
Variadic functions (`...`) are not currently supported for proper ABI compliance.

**Root Cause**: The `CallOp` structure lacks an `is_variadic` field, so the code generator cannot detect if a function call is to a variadic function.

**Impact**:
- Float arguments are not copied to both XMM and GPR registers as required by System V AMD64 ABI
- Float arguments are not promoted to double before passing
- Functions like `printf`, `scanf`, and custom variadic functions may not work correctly

**Workaround**: Use non-variadic alternatives or extern "C" wrappers

## Legacy Operand-Based Code Path
The IR converter has two code paths for function calls:
1. **Modern path**: Uses `CallOp` typed payload (preferred)
2. **Legacy path**: Uses operand-based instruction format

The legacy path:
- Cannot detect variadic functions
- Has limited ABI feature support
- Exists for backward compatibility (purpose unclear - possibly dead code)

**Question for maintainers**: Can the legacy operand-based path be removed? All current tests appear to use the typed payload path.

## Stack Argument Handling with Mixed Types  
The stack argument overflow logic uses a simplified heuristic based on integer register count.

**Works correctly when**:
- All integer arguments OR all float arguments fit in registers
- Standard function signatures

**May have issues with**:
- Complex mixed-type signatures that overflow both register pools  
- Example edge case: `func(double×5, int×10)` - 5 doubles in XMM0-4, but 7th-10th ints need stack

## Recommendations for Production Use

1. **Add `is_variadic` field to `CallOp`**:
   ```cpp
   struct CallOp {
       // ... existing fields ...
       bool is_variadic = false;  // NEW: Detect varargs at call site
   };
   ```

2. **Implement proper varargs handling**:
   - When `is_variadic` is true and argument is float:
     - Promote float→double
     - Copy XMM value to corresponding GPR at same position
   
3. **Remove legacy operand-based path** (if not needed):
   - Simplifies code
   - Reduces maintenance burden
   - Eliminates ABI inconsistencies

4. **Enhance stack overflow logic**:
   - Track both int and float register usage separately
   - Correctly handle mixed-type overflow scenarios

These limitations do not affect the core functionality for normal (non-variadic) functions, as demonstrated by the test cases.
