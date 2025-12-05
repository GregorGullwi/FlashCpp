# Object File Analysis Tools

This directory contains tools to analyze object files and validate lambda code generation in FlashCpp.

## Tools

### 1. analyze_obj_symbols.ps1 (Windows/PowerShell)
Analyzes object files using Windows `dumpbin.exe`.

**Usage:**
```powershell
.\tests\analyze_obj_symbols.ps1 <file.obj>
```

**Example:**
```powershell
# Compile a test file
.\x64\Debug\FlashCpp.exe tests\test_lambda_cpp20_comprehensive.cpp

# Analyze the generated object file
.\tests\analyze_obj_symbols.ps1 test_lambda_cpp20_comprehensive.obj
```

**Output:**
- All symbols in the object file
- Categorized symbols (defined, undefined, external)
- Section information
- Disassembly of code

### 2. analyze_obj_symbols.sh (Linux/bash)
Analyzes object files using `objdump` or `llvm-objdump`.

**Usage:**
```bash
./tests/analyze_obj_symbols.sh <file.o>
```

**Example:**
```bash
# If you have a COFF object file from FlashCpp
./tests/analyze_obj_symbols.sh test_lambda_cpp20_comprehensive.obj

# Works with both ELF and COFF formats
```

**Features:**
- Automatically detects available tools (objdump, llvm-objdump, nm)
- Supports both ELF (Linux) and COFF (Windows) formats
- Demangles C++ symbols if c++filt is available
- Shows lambda-specific symbols

### 3. test_lambda_comprehensive_detailed.ps1 (Windows)
Comprehensive test script specifically for test_lambda_cpp20_comprehensive.cpp.

**Usage:**
```powershell
.\tests\test_lambda_comprehensive_detailed.ps1
```

**What it does:**
1. Compiles test_lambda_cpp20_comprehensive.cpp
2. Analyzes object file symbols
3. Counts lambda operator() and __invoke symbols
4. Links the object file
5. Runs the executable
6. Validates exit code (should be 135 = 27 tests × 5)

**Output:**
- Compilation status
- Symbol analysis (lambda-related symbols)
- Detailed link errors if any
- Execution result and validation

## Enhanced Test Script

### test_reference_files.ps1
The main test script now includes enhanced error reporting.

**New Features:**
- Shows ALL link errors (not just the first one)
- Shows unresolved external symbols
- **NEW: Detailed link errors section at the end**

**Example Output:**
```
=== Files that failed to link ===
  - some_test.cpp

==============================================
DETAILED LINK ERRORS (for easy debugging)
==============================================

=== some_test.cpp ===

Link Errors:
  some_test.obj : error LNK2019: unresolved external symbol

Unresolved External Symbols:
  unresolved external symbol "void __cdecl foo(void)" (?foo@@YAXXZ)
```

## Validating Lambda Code Generation

To validate that lambda code is being generated correctly:

### Step 1: Compile
```powershell
.\x64\Debug\FlashCpp.exe tests\test_lambda_cpp20_comprehensive.cpp
```

### Step 2: Analyze Symbols
```powershell
.\tests\analyze_obj_symbols.ps1 test_lambda_cpp20_comprehensive.obj
```

### Step 3: Look for Lambda Symbols
The output should show:
- `operator()` symbols for each lambda
- `__invoke` symbols for lambdas without captures
- Closure struct definitions

### Step 4: Check for Missing Symbols
Look at the "Undefined symbols" section. These need to be resolved during linking:
- Standard library functions (expected)
- Your own functions (check they're defined)
- Lambda operator() functions (should NOT be undefined - they should be defined)

## Common Issues

### Issue: "unresolved external symbol ... operator()"
**Cause:** Lambda operator() function not generated or not exported
**Solution:** Check that generateLambdaOperatorCallFunction() is being called

### Issue: "unresolved external symbol ... __invoke"
**Cause:** __invoke function not generated for capture-less lambda
**Solution:** Check that generateLambdaInvokeFunction() is being called

### Issue: No lambda symbols in object file
**Cause:** Lambda code generation skipped or failed
**Solution:** Check parser creates LambdaInfo correctly and calls code generation

## Example: Analyzing test_lambda_cpp20_comprehensive.cpp

```powershell
# Full analysis
.\tests\test_lambda_comprehensive_detailed.ps1
```

Expected output shows:
```
=== ANALYZING OBJECT FILE ===
Total lambda-related symbols: 54
  operator() symbols: 27
  __invoke symbols: 5

Lambda operator() symbols:
  ...struct __lambda_0 operator() symbols...
  ...struct __lambda_1 operator() symbols...
  
=== LINKING ===
LINKING SUCCESSFUL

=== RUNNING TEST ===
Exit code: 135
TEST PASSED: All 27 lambda tests returned expected value (27 * 5 = 135)
```

## Cross-Platform Usage

### On Windows:
```powershell
.\tests\analyze_obj_symbols.ps1 file.obj
```

### On Linux (with COFF object from FlashCpp):
```bash
./tests/analyze_obj_symbols.sh file.obj
```

### On Linux (native ELF object):
```bash
./tests/analyze_obj_symbols.sh file.o
```

Both scripts provide similar information, formatted for their respective platforms.
