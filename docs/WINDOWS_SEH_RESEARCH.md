# Windows Structured Exception Handling (SEH) Research

## Overview

Windows SEH is a Microsoft-specific exception handling mechanism that differs from C++ exceptions. It uses `__try`, `__except`, and `__finally` keywords instead of `try`, `catch`, and `throw`.

## Key Differences: SEH vs C++ Exceptions

### C++ Exceptions (Already Implemented in FlashCpp)
- **Keywords**: `try`, `catch`, `throw`
- **Type-safe**: Catches specific exception types
- **Object-oriented**: Throws and catches objects
- **Stack unwinding**: Calls destructors automatically
- **Platform**: Cross-platform (Itanium C++ ABI on Linux, MSVC on Windows)
- **Implementation**: Uses `.eh_frame` and `.gcc_except_table` on Linux

### Windows SEH
- **Keywords**: `__try`, `__except`, `__finally`, `__leave`
- **Filter-based**: Uses filter expressions to decide whether to handle
- **Low-level**: Handles hardware exceptions (access violations, divide by zero)
- **Platform**: Windows-only (MSVC extension)
- **Implementation**: Uses `.pdata` and `.xdata` sections in COFF/PE files

## SEH Syntax

### try-except
```cpp
__try {
    // Protected code
    int* p = nullptr;
    *p = 42;  // Access violation
}
__except (filter_expression) {
    // Exception handler
    printf("Caught exception\n");
}
```

### try-finally
```cpp
__try {
    // Protected code
    FILE* f = fopen("file.txt", "r");
    // ... use file ...
}
__finally {
    // Cleanup code (always executed)
    if (f) fclose(f);
}
```

### Filter Expressions
The `__except` clause takes a filter expression that returns:
- `EXCEPTION_EXECUTE_HANDLER` (1): Handle the exception
- `EXCEPTION_CONTINUE_SEARCH` (0): Continue searching for a handler
- `EXCEPTION_CONTINUE_EXECUTION` (-1): Resume execution at the point of exception

### __leave Keyword
Exits a `__try` block without triggering exception handling:
```cpp
__try {
    if (error) __leave;  // Jump to __finally or after __except
}
__finally {
    // Cleanup
}
```

## Windows x64 Exception Handling Implementation

### Data Structures

#### RUNTIME_FUNCTION (in .pdata section)
```cpp
struct RUNTIME_FUNCTION {
    ULONG BeginAddress;      // RVA of function start
    ULONG EndAddress;        // RVA of function end
    ULONG UnwindData;        // RVA to UNWIND_INFO
};
```

#### UNWIND_INFO (in .xdata section)
```cpp
struct UNWIND_INFO {
    UBYTE Version : 3;           // Currently 1
    UBYTE Flags : 5;             // UNW_FLAG_EHANDLER, UNW_FLAG_UHANDLER, UNW_FLAG_CHAININFO
    UBYTE SizeOfProlog;          // Prolog size in bytes
    UBYTE CountOfCodes;          // Number of unwind codes
    UBYTE FrameRegister : 4;     // Frame pointer register (0 = none)
    UBYTE FrameOffset : 4;       // Frame offset (scaled by 16)
    UNWIND_CODE UnwindCode[1];   // Array of unwind codes
    // Optional: Exception handler RVA
    // Optional: Language-specific handler data
};
```

### Unwind Codes
Describe how the prolog modifies the stack:
- `UWOP_PUSH_NONVOL`: Push a nonvolatile register
- `UWOP_ALLOC_LARGE`: Allocate large stack space
- `UWOP_ALLOC_SMALL`: Allocate small stack space (8-128 bytes)
- `UWOP_SET_FPREG`: Establish frame pointer
- `UWOP_SAVE_NONVOL`: Save nonvolatile register to stack
- `UWOP_SAVE_XMM128`: Save XMM register
- `UWOP_PUSH_MACHFRAME`: Hardware interrupt/exception frame

### Exception Handler
```cpp
typedef EXCEPTION_DISPOSITION (*PEXCEPTION_ROUTINE) (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN ULONG64 EstablisherFrame,
    IN OUT PCONTEXT ContextRecord,
    IN OUT PDISPATCHER_CONTEXT DispatcherContext
);
```

## Implementation Requirements for FlashCpp

### 1. Lexer Changes
Add keywords to `Lexer.h`:
- `__try`
- `__except`
- `__finally`
- `__leave`

### 2. Parser Changes
Create AST nodes:
- `TryExceptStatementNode`
- `TryFinallyStatementNode`
- `ExceptFilterNode`
- `LeaveStatementNode`

### 3. IR Opcodes
Add to `IRTypes.h`:
- `SehTryBegin`
- `SehTryEnd`
- `SehExceptBegin` (with filter expression)
- `SehExceptEnd`
- `SehFinallyBegin`
- `SehFinallyEnd`
- `SehLeave`

### 4. Code Generation (Windows COFF only)
Implement in `ObjFileWriter.h`:
- Generate `.pdata` section with `RUNTIME_FUNCTION` entries
- Generate `.xdata` section with `UNWIND_INFO` structures
- Track prolog operations to generate unwind codes
- Register exception handlers with proper RVAs
- Implement filter expression evaluation

## Compatibility Notes

1. **SEH and C++ Exceptions Don't Mix Well**
   - Using SEH in C++ code can skip destructor calls
   - MSVC provides `/EHa` flag to make SEH call destructors
   - FlashCpp should warn when mixing SEH with C++ exceptions

2. **Platform-Specific**
   - SEH is Windows-only
   - Should only be available when targeting Windows (COFF output)
   - Linux builds should reject `__try`/`__except`/`__finally`

3. **Hardware Exceptions**
   - SEH can catch hardware exceptions (access violations, divide by zero)
   - Requires OS support (Windows exception dispatcher)
   - Filter expressions run in exception context

## References

- [Microsoft Docs: Structured Exception Handling](https://learn.microsoft.com/en-us/cpp/cpp/structured-exception-handling-c-cpp)
- [Microsoft Docs: x64 Exception Handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64)
- [PE Format Specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

