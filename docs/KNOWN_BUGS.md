# Known Bugs in FlashCpp

This document tracks confirmed bugs in the FlashCpp compiler that need to be fixed.

**Last Updated**: 2025-12-12

## Bug #1: Incorrect Line Numbers for Errors in Included Files

**Status**: ✅ **FIXED**  
**Severity**: Medium  
**Affects**: Error reporting, debugging experience

### Description

When the compiler encountered an error in a file included via `#include`, it reported an incorrect line number. The line number was off because the parser's error recovery mechanism would wrap errors with a token from earlier in the parse tree (e.g., the function declaration's start token instead of the actual error token).

### Fix

**Fixed in commit 937b067 and follow-up commit:**

1. **Line Number Mapping** (commit 937b067): Added infrastructure to map preprocessed line numbers to source file line numbers using the `line_map_` in `src/Parser.h`.

2. **Error Token Preservation** (this commit): Modified `parse_top_level_node()` in `src/Parser.cpp` to preserve the original error token instead of wrapping it with a new token. Changed from calling `saved_position.error()` to `saved_position.propagate()`, which maintains the original error context.

### Verification

Testing with an intentional error on line 35 of an included file now correctly reports:
```
test_varargs_helper.c:35:27: error: Missing identifier
      INTENTIONAL_ERROR_HERE;  
                            ^
```

Previously would have incorrectly reported line 18 or 26.

### Test Case

Files: `tests/test_varargs.cpp` and `tests/test_varargs_helper.c`

Can verify the fix by adding an intentional error to line 35 of `test_varargs_helper.c`:
```c
INTENTIONAL_ERROR_HERE;  // Now correctly reports line 35
```

### Root Cause (FIXED)

The issue had two parts, both now resolved:

1. **Error Token Selection** (NOW FIXED): When a parse error occurred deep in the parse tree (e.g., in a function body), the error would bubble up and get re-wrapped with an earlier token (e.g., the function declaration's start token). This was because `parse_top_level_node()` called `saved_position.error()` which created a new error with the saved token. Fixed by calling `saved_position.propagate()` instead, which preserves the original error token.

2. **Line Number Mapping** (FIXED): The line number mapping from preprocessed output to source file is now correctly implemented in `src/Parser.h` using the `line_map_` structure.

### Affected Code (FIXED)

- `src/Parser.h` - Error formatting with line number mapping (commit 937b067)
- `src/Parser.cpp` - Error propagation in `parse_top_level_node()` (this commit)
- `src/FileReader.h` - Preprocessor and file inclusion handling (line_map infrastructure)

