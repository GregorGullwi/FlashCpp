# Known Bugs in FlashCpp

This document tracks confirmed bugs in the FlashCpp compiler that need to be fixed.

**Last Updated**: 2025-12-12

## Bug #1: Incorrect Line Numbers for Errors in Included Files

**Status**: 🐛 **CONFIRMED BUG**  
**Severity**: Medium  
**Affects**: Error reporting, debugging experience

### Description

When the compiler encounters an error in a file included via `#include`, it reports an incorrect line number. The line number appears to be off by approximately 17 lines in the tested case.

### Reproduction

1. Create a file `test_varargs_helper.c` with an intentional error on line 35:
```c
int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    
    INTENTIONAL_ERROR_HERE;  // Line 35: This should cause an error
    
    va_end(args);
    return sum;
}
```

2. Include this file from `test_varargs.cpp`:
```cpp
extern "C" {
   #include "test_varargs_helper.c"
}
```

3. Compile with FlashCpp

### Expected Behavior

Error message should report:
```
test_varargs_helper.c:35:5: error: Missing identifier: INTENTIONAL_ERROR_HERE
    INTENTIONAL_ERROR_HERE;
    ^
```

### Actual Behavior

Error message reports:
```
test_varargs_helper.c:18:4: error: Failed to parse top-level construct
  int sum_ints(int count, ...) {
     ^
```

The line number is wrong (18 instead of 35), and it points to a comment line instead of the actual error location.

### Root Cause

The issue has two parts:

1. **Error Token Selection**: When a parse error occurs deep in the parse tree (e.g., in a function body), the error bubbles up and gets re-wrapped with an earlier token (e.g., the function declaration's start token). This is standard error recovery behavior, but it means the error is reported at the function declaration rather than the actual error location.

2. **Line Number Mapping** (PARTIALLY FIXED): The line number is now correctly mapped from preprocessed output to source file using the line_map. However, because the wrong token is being used (see #1), it still reports the wrong line.

### Status

**Partially Fixed** - The line number mapping infrastructure is now in place and working correctly. However, the parser's error recovery mechanism still causes errors to be reported with the wrong token. A complete fix would require changing how errors bubble up through the parser to preserve the original error token.

### Affected Code

- `src/FileReader.h` - Preprocessor and file inclusion handling
- `src/Lexer.h` - Token line number tracking
- `src/Parser.cpp` - Error reporting

### Suggested Fix

1. Check how `FileReader` handles line number tracking when processing `#include` directives
2. Ensure tokens from included files have their line numbers correctly set relative to the included file, not the including file
3. Verify that error messages correctly report the file name and line number from the token's metadata

### Test Case

File: `tests/test_varargs.cpp` and `tests/test_varargs_helper.c`

Can reproduce by adding an intentional error to line 35 of `test_varargs_helper.c`:
```c
INTENTIONAL_ERROR_HERE;  // Should report line 35, not line 18
```

### Priority

Medium - This doesn't affect code generation, but makes debugging significantly harder when errors occur in included files. Users may spend time looking at the wrong line of code.
