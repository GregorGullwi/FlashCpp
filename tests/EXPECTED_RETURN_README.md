# Expected Return Value Testing

## Overview

The FlashCpp test infrastructure now supports verifying that test executables return the correct values. This ensures that test files not only compile and link properly, but also execute correctly.

## How to Specify Expected Return Values

Add a comment at the beginning of your test file with the format:

```cpp
// EXPECTED_RETURN: <value>
```

Where `<value>` is the expected exit code (return value from main).

### Examples

```cpp
// EXPECTED_RETURN: 0
int main() {
    return 0;
}
```

```cpp
// EXPECTED_RETURN: 42
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(20, 22);
}
```

```cpp
// EXPECTED_RETURN: 45
int test_sum() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += i;
    }
    return sum;  // 0+1+2+...+9 = 45
}

int main() {
    return test_sum();
}
```

## Test Scripts

### Linux/Ubuntu (run_all_tests.sh)

The bash script:
1. Compiles each test file with FlashCpp
2. Links the object file with clang++
3. If `EXPECTED_RETURN` is specified, runs the executable and verifies the return value
4. Reports pass/fail for compilation, linking, and execution

### Windows (test_reference_files.ps1)

The PowerShell script:
1. Compiles each test file with FlashCpp
2. Links the object file with MSVC linker
3. If `EXPECTED_RETURN` is specified, runs the executable and verifies the return value
4. Reports pass/fail for compilation, linking, and execution

## Test Report Format

Both scripts now report three categories:
- **Compile**: Did the file compile successfully?
- **Link**: Did the object file link successfully?
- **Run**: Did the executable return the expected value?

Example output:
```
[1/100] Testing test_simple_add.cpp... OK (returned 30)
[2/100] Testing test_comparison.cpp... OK (returned 42)
[3/100] Testing test_basic.cpp... OK (returned 0)
```

If a test returns the wrong value:
```
[5/100] Testing test_arithmetic.cpp...
[RUN FAIL] test_arithmetic.cpp - Expected return: 7, got: 8
```

## Guidelines

1. **Add EXPECTED_RETURN to all new tests**: When creating a new test file, include the expected return value comment.

2. **Return values should be in range 0-255**: On most systems, exit codes are limited to 0-255. Values outside this range may be truncated.

3. **Document complex calculations**: If the expected return value isn't obvious, add a comment explaining it:
   ```cpp
   // EXPECTED_RETURN: 45
   // Sum of 0+1+2+...+9 = 45
   ```

4. **Tests without EXPECTED_RETURN**: Tests that don't specify an expected return value will still be compiled and linked, but won't have their return value verified. This is useful for:
   - Tests that are primarily checking for compilation/linking (e.g., template instantiation tests)
   - Tests that rely on external resources or have non-deterministic return values

## Adding Expected Returns to Existing Tests

To gradually add expected return values to existing tests:

1. Review the test file and understand what it's testing
2. Calculate or determine the expected return value
3. Add the `// EXPECTED_RETURN: <value>` comment at the top of the file
4. Run the test to verify it passes

## CI Integration

Both GitHub Actions workflows (Windows and Ubuntu) automatically run these test scripts on every commit. Tests that return incorrect values will cause the CI build to fail, ensuring code quality.
