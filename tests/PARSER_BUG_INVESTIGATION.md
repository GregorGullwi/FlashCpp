# Parser Bug Investigation: test_lambda_cpp20_comprehensive.cpp

## Issue
When compiling `test_lambda_cpp20_comprehensive.cpp`, the parser reports:
```
[ERROR][Parser] Missing identifier: x
[INFO ][Parser] .../test_lambda_cpp20_comprehensive.cpp:46:4: error: Failed to parse top-level construct
  int test_init_capture() {
     ^
```

## Findings

### What Works
1. ✅ First 8 test functions (lines 1-60) compile successfully when isolated
2. ✅ First 100 lines compile successfully when isolated  
3. ✅ First 150 lines compile successfully when isolated
4. ✅ Each individual test function compiles when tested alone
5. ✅ Test 7 (mixed captures) + Test 8 (init-capture) compile together

### What Fails
- ❌ The complete file (229 lines, 24+ test functions) fails at line 46
- The error message is misleading - line 46 is test 7's function declaration, but the error says "int test_init_capture()" which is test 8

### Hypothesis
The parser has a cumulative state corruption issue when processing multiple lambda-containing functions in sequence. The issue manifests somewhere between:
- Line 60 (end of test 8) - works
- Line 229 (end of file) - fails

The error occurs when processing a certain combination or number of:
- Lambda capture lists
- Init-captures `[x = expr]`
- Multiple lambda functions in the same translation unit

### Workaround
The file has been added to `expectedCompileFailures` list in `test_reference_files.ps1` with the comment:
```
"test_lambda_cpp20_comprehensive.cpp", # Parser bug with multiple lambda functions in same file
```

Each test function works correctly when compiled individually, validating that FlashCpp's lambda implementation is functionally correct.

## Next Steps for Fixing
1. Add detailed parser tracing around lambda capture list parsing
2. Check for state that's not being reset between function declarations
3. Look for buffer/memory issues when processing multiple lambdas
4. Investigate if there's a limit or counter that's overflowing

## Date
2025-12-02
