# SEH Hardware Exception Handling - Implementation Plan

## Current Status

### What Works ✅
- **Control Flow SEH**: All SEH control flow constructs work correctly:
  - `__try/__except` blocks execute properly
  - `__try/__finally` blocks execute properly
  - `__leave` statement works correctly
  - Nested SEH blocks work
  - Filter expressions are evaluated (at compile time)
  - Handlers execute when explicitly jumped to

### What Doesn't Work ❌
- **Hardware Exception Catching**: Real Windows hardware exceptions (access violations, divide-by-zero, etc.) are NOT caught by `__except` handlers
- **Test Result**: `test_seh_hardware.exe` crashes with exit code -1073741819 (0xC0000005 = EXCEPTION_ACCESS_VIOLATION)
- **Root Cause**: The exception propagates to Windows instead of being caught by our SEH handler

## Problem Analysis

### Current Implementation Architecture

1. **Filter Expression Evaluation** (CodeGen.h:5820-5830)
   - Filter expressions are evaluated **at compile time** during IR generation
   - Result is stored in a TempVar
   - This works for control flow but NOT for runtime exceptions

2. **Scope Table Generation** (ObjFileWriter.h:910-980)
   - Generates SEH scope tables in `.xdata` section
   - Format:
     ```
     DWORD Count
     For each entry:
       DWORD BeginAddress  (RVA of try block start)
       DWORD EndAddress    (RVA of try block end)
       DWORD HandlerAddress (currently: RVA of handler code)
       DWORD JumpTarget     (currently: 0 for __except, 1 for __finally)
     ```

3. **Exception Handler Registration**
   - `.pdata` section contains RUNTIME_FUNCTION entries
   - `.xdata` section contains UNWIND_INFO with reference to `__C_specific_handler`
   - Scope table follows UNWIND_INFO

### What Windows Expects for Hardware Exceptions

According to research (Rust SEH article, ReactOS source):

```c
typedef struct _SCOPE_TABLE_AMD64 {
    DWORD Count;
    struct {
        DWORD BeginAddress;    // start RVA of the range
        DWORD EndAddress;      // end RVA of the range
        
        // if JumpTarget != 0:
        //   filter funclet RVA OR one of:
        //   EXCEPTION_EXECUTE_HANDLER    = 1
        //   EXCEPTION_CONTINUE_SEARCH    = 0
        //   EXCEPTION_CONTINUE_EXECUTION = -1
        // else:
        //   __finally handler (destructor)
        DWORD HandlerAddress;
        
        // RVA of __except handler code
        DWORD JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE_AMD64;
```

### The Critical Difference

**Current Implementation:**
- `HandlerAddress` = RVA of handler code
- `JumpTarget` = 0 for `__except`

**What Windows Needs:**
- `HandlerAddress` = RVA of **filter function** (or constant like EXCEPTION_EXECUTE_HANDLER=1)
- `JumpTarget` = RVA of **handler code** (the `__except` block body)

## Implementation Options

### Option 1: Generate Filter Functions (Full Solution)

**Approach:**
1. For each `__except` block, generate a separate **filter function**
2. Filter function signature:
   ```c
   LONG filter_func(EXCEPTION_POINTERS* ExceptionPointers, PVOID EstablisherFrame)
   ```
3. Filter function evaluates the filter expression at runtime
4. Update scope table:
   - `HandlerAddress` = RVA of filter function
   - `JumpTarget` = RVA of handler code

**Pros:**
- Supports all filter expressions (including `GetExceptionCode()`, `GetExceptionInformation()`)
- Fully compliant with Windows SEH model
- Enables runtime exception filtering

**Cons:**
- Significant architectural change required
- Need to generate separate functions for filters
- Complex implementation

**Implementation Steps:**
1. Modify `visitSehTryExceptStatementNode()` to generate filter function
2. Filter function needs access to exception information
3. Update IR to include filter function generation
4. Modify `IRConverter` to emit filter function code
5. Update scope table generation to use filter function RVA

### Option 2: Constant Filter Optimization (Quick Fix)

**Approach:**
1. For constant filter expressions (like `EXCEPTION_EXECUTE_HANDLER`), use the constant directly
2. Update scope table generation:
   - If filter is constant: `HandlerAddress` = constant value (1, 0, or -1)
   - `JumpTarget` = RVA of handler code

**Pros:**
- Simpler to implement
- Works for most common cases (EXCEPTION_EXECUTE_HANDLER)
- No need to generate filter functions for constants

**Cons:**
- Doesn't support `GetExceptionCode()` or `GetExceptionInformation()`
- Limited to constant filter expressions
- Not a complete solution

**Implementation Steps:**
1. Detect if filter expression is a compile-time constant
2. If constant, store the value (not TempVar)
3. Update scope table generation:
   - Check if filter is constant
   - If yes: use constant value for `HandlerAddress`
   - If no: generate filter function (or error for now)
4. Set `JumpTarget` to handler code RVA

### Option 3: Hybrid Approach (Recommended)

**Approach:**
1. Implement Option 2 first (constant filters)
2. Add support for filter functions later (Option 1)
3. Detect filter type and choose appropriate method

**Pros:**
- Incremental implementation
- Quick win for common cases
- Path to full solution

**Cons:**
- Two code paths to maintain
- Still requires eventual full implementation

## Required Code Changes

### Phase 1: Constant Filter Support

1. **CodeGen.h** (`visitSehTryExceptStatementNode`)
   - Detect if filter expression is constant
   - Store constant value vs TempVar
   - Update `SehExceptBeginOp` to include filter type

2. **IRTypes.h** (`SehExceptBeginOp`)
   - Add field: `bool is_constant_filter`
   - Add field: `int32_t constant_filter_value`

3. **ObjectFileCommon.h** (`SehExceptHandlerInfo`)
   - Add field: `bool is_constant_filter`
   - Add field: `int32_t constant_filter_value`

4. **IRConverter.h** (`handleSehExceptBegin`)
   - Store filter type and value in `SehExceptHandlerInfo`

5. **ObjFileWriter.h** (scope table generation)
   - Check `is_constant_filter`
   - If true: use `constant_filter_value` for `HandlerAddress`
   - Set `JumpTarget` to handler code RVA (not 0!)

### Phase 2: Filter Function Support

1. **CodeGen.h**
   - Generate filter function for non-constant expressions
   - Pass exception information to filter
   - Support `GetExceptionCode()`, `GetExceptionInformation()`

2. **IRConverter.h**
   - Emit filter function code
   - Handle filter function calling convention

3. **ObjFileWriter.h**
   - Use filter function RVA for `HandlerAddress`

## Testing Strategy

### Test Cases

1. **Constant Filter - EXCEPTION_EXECUTE_HANDLER**
   ```cpp
   __try { *null_ptr = 42; }
   __except(EXCEPTION_EXECUTE_HANDLER) { return 100; }
   ```

2. **Constant Filter - EXCEPTION_CONTINUE_SEARCH**
   ```cpp
   __try { *null_ptr = 42; }
   __except(EXCEPTION_CONTINUE_SEARCH) { /* not executed */ }
   ```

3. **GetExceptionCode() Filter** (Phase 2)
   ```cpp
   __except(GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION 
            ? EXCEPTION_EXECUTE_HANDLER 
            : EXCEPTION_CONTINUE_SEARCH)
   ```

4. **Nested Exceptions with Hardware Exceptions**

### Validation

- Compare generated `.xdata` with MSVC output
- Use `dumpbin /unwindinfo` to verify scope table format
- Test with actual hardware exceptions
- Verify exception codes are correct

## References

- [Implementing SEH in Rust](https://engineering.zeroitlab.com/2022/03/13/rust-seh/)
- [ReactOS __C_specific_handler implementation](https://github.com/reactos/reactos)
- [Microsoft Docs: x64 Exception Handling](https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64)
- Current test files:
  - `tests/FlashCppTest/FlashCppTest/FlashCppTest/test_seh.cpp` (control flow - works)
  - `tests/FlashCppTest/FlashCppTest/FlashCppTest/test_seh_hardware.cpp` (hardware - fails)

## Next Steps

1. ✅ Create this planning document
2. ⏳ Decide on implementation approach (Option 1, 2, or 3)
3. ⏳ Implement chosen approach
4. ⏳ Test with hardware exceptions
5. ⏳ Compare with MSVC-generated code
6. ⏳ Iterate until hardware exceptions are caught correctly

