# Known Limitations - Linux ABI Implementation

## Varargs Functions
The current implementation has limited support for variadic functions (`...`). 
- Regular (non-variadic) functions work correctly with separate int/float register pools
- Variadic functions may not correctly handle the System V AMD64 ABI requirement to pass float arguments in BOTH XMM and GPR registers

## Stack Argument Handling with Mixed Types  
The stack argument overflow logic currently uses a simplified heuristic based on integer register count.
- Works correctly when all integer OR all float arguments fit in registers
- May have issues with complex mixed-type signatures that overflow both register pools
- Example edge case: `func(double×5, int×10)` - 5 doubles in XMM0-4, but 7th-10th ints need stack

## Recommendation
For production use, consider:
1. Adding explicit varargs detection and handling
2. Implementing proper stack overflow logic that tracks both int and float register usage
3. Adding more comprehensive tests for edge cases

These limitations do not affect the core functionality demonstrated by the test cases.
