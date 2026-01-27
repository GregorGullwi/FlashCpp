# Multiple Catch Handler Support - Implementation Status

## Overview
This document describes the implementation work done to support multiple catch handlers per try block, addressing the TODO comment in `src/LSDAGenerator.h:190`:
```cpp
// TODO: Support multiple catch handlers with action chaining
```

## What Was Implemented

### Action Table Chaining (COMPLETED)
The `encode_action_table()` function in `LSDAGenerator.h` now correctly generates chained action entries for multiple catch handlers:

**Before:**
- Only the first catch handler was encoded in the action table
- All other handlers were ignored
- The next_offset field was always set to 0 (no chaining)

**After:**
- All catch handlers in a try block are now encoded as action table entries
- Each entry contains:
  - `type_filter`: 1-based index into the type table for type matching
  - `next_offset`: 1 for non-final entries (points to next handler), 0 for final entry
- Action entries are laid out sequentially in the action table
- The personality routine can now traverse the chain to find a matching handler

**Example:** For a try block with 3 catch handlers (char, int, catch-all):
```
Action Table:
  Entry 0: type_filter=1 (char),   next_offset=1  →  Entry 1
  Entry 1: type_filter=2 (int),    next_offset=1  →  Entry 2
  Entry 2: type_filter=0 (catch-all), next_offset=0  (end of chain)
```

### Verification
The test file `tests/test_action_table_chaining.cpp` demonstrates the correct generation:
```bash
$ FlashCpp tests/test_action_table_chaining.cpp -o output.o -v
[DEBUG] Action chaining: entry 0 -> entry 1 next_offset=1
[DEBUG] Action chaining: entry 1 -> entry 2 next_offset=1
[DEBUG] Action table size: 6 bytes
```

## What Remains (Landing Pad Architecture)

While the action table now correctly supports multiple catch handlers, **full runtime support requires refactoring the landing pad code generation** in `IRConverter.h`.

### Current Landing Pad Architecture (Problematic)
```
try {
    // code
} catch (char c) {    // Landing pad 1 at offset X
    // __cxa_begin_catch called here
} catch (int i) {      // Landing pad 2 at offset Y  
    // __cxa_begin_catch called here
} catch (...) {        // Landing pad 3 at offset Z
    // __cxa_begin_catch called here
}
```

Each catch handler generates:
- Separate entry point (landing pad)
- Own `__cxa_begin_catch` call
- Own exception handling code

### Required Landing Pad Architecture (Itanium C++ ABI)
```
try {
    // code
} catch (char c) {
    // handler body
} catch (int i) {
    // handler body  
} catch (...) {
    // handler body
}

// Unified landing pad at offset X (shared by all handlers):
landing_pad:
    ; RAX = exception object pointer (from personality routine)
    ; RDX = selector value (which handler matched, from personality routine)
    
    call __cxa_begin_catch  ; Call once
    
    ; Dispatch based on selector:
    cmp rdx, 1
    je .handler_char
    cmp rdx, 2
    je .handler_int
    jmp .handler_catchall
    
.handler_char:
    ; char handler body
    ...
.handler_int:
    ; int handler body
    ...
.handler_catchall:
    ; catch-all handler body
    ...
```

### Why This Matters
The Itanium C++ ABI personality routine (`__gxx_personality_v0`):
1. Searches the action table to find a matching type
2. Sets RAX to the exception object pointer
3. Sets RDX to the selector value (indicating which handler matched)
4. Jumps to the landing pad

The landing pad MUST:
- Be a single unified entry point for all handlers in the try block
- Read the selector from RDX
- Dispatch to the appropriate handler body

### Impact
Currently, even though the action table is correct:
- Only the first catch handler will execute at runtime
- Type matching may appear to work incorrectly
- Example: `throw 42;` with `catch (char)` and `catch (int)` will catch as `char`

This is because each handler has its own landing pad and `__cxa_begin_catch` call, bypassing the selector dispatch mechanism.

## Implementation Approach for Landing Pad Fix

### Changes Required in `IRConverter.h`

1. **Modify `handleCatchBegin()`**:
   - Only generate `__cxa_begin_catch` for the FIRST catch handler in a try block
   - For subsequent handlers, skip to generating the dispatch logic
   - Track which handlers belong to the same try block

2. **Add Landing Pad Dispatch Generation**:
   ```cpp
   void generateUnifiedLandingPad(const TryBlock& try_block) {
       // Save selector value from RDX
       emitMovRegToStack(X64Register::RDX, selector_stack_offset);
       
       // Call __cxa_begin_catch once
       emitMovRegReg(X64Register::RDI, X64Register::RAX);
       emitCall("__cxa_begin_catch");
       
       // Generate dispatch based on selector
       for (size_t i = 0; i < try_block.catch_handlers.size(); ++i) {
           if (i > 0) {
               emitLabel(get_handler_dispatch_label(i));
           }
           emitCmpStackImmediate(selector_stack_offset, i + 1);
           emitJumpIfEqual(get_handler_body_label(i));
       }
       
       // Jump to first handler body by default
       emitJump(get_handler_body_label(0));
   }
   ```

3. **Update IR Structure**:
   - Modify `TryBlock` to track all handlers as a group
   - Add metadata to identify the first vs. subsequent handlers
   - Possibly add new IR opcodes for dispatch logic

### Estimated Complexity
- **Refactoring landing pad generation**: 6-8 hours
- **Testing and debugging**: 4-6 hours
- **Total**: ~10-14 hours of development work

## Testing Strategy

### Current Tests
- ✅ `tests/test_exceptions_basic.cpp`: Single catch handler (works)
- ⚠️ `tests/test_exceptions_nested.cpp`: Multiple catch handlers (action table correct, runtime needs fix)
- ✅ `tests/test_action_table_chaining.cpp`: Demonstrates action table generation (works)

### Additional Tests Needed (After Landing Pad Fix)
1. Type matching priority: earlier handlers should be tried first
2. Inheritance hierarchy: derived exception types
3. Catch-all (`...`) as final handler
4. Mix of reference and value catch parameters
5. Nested try blocks with multiple handlers

## References

### Itanium C++ ABI Exception Handling
- Landing pad calling convention: RAX = exception object, RDX = selector
- Action table format and chaining: https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
- Type matching and RTTI: type_info pointers in type table

### Related Files
- `src/LSDAGenerator.h`: Action table generation (NOW COMPLETE)
- `src/IRConverter.h`: Landing pad code generation (NEEDS REFACTORING)
- `src/ElfFileWriter.h`: LSDA data structure conversion
- `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md`: Exception handling design notes

## Conclusion

The action table chaining is now fully implemented and correctly generates chained action entries for multiple catch handlers. This addresses the TODO comment in the code.

However, **full multiple catch handler support requires refactoring the landing pad architecture** to use a unified landing pad with selector-based dispatch, as specified by the Itanium C++ ABI. This is a larger architectural change that should be done in a follow-up PR.

The current implementation:
- ✅ Single catch handler per try: Works correctly
- ⚠️ Multiple catch handlers: Action table correct, awaiting landing pad refactor
- ✅ Basic exception handling: Unchanged and working
