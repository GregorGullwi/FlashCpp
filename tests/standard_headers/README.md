# Standard Library Header Tests

This directory contains test files for C++ standard library headers. These tests are **not** run by `tests/run_all_tests.sh` because they currently don't compile or timeout due to missing FlashCpp features.

## Why These Tests Are Separate

The main test suite (`tests/run_all_tests.sh`) is designed to test FlashCpp's core functionality with fast execution. These standard header tests:

1. **Cause timeouts** - Complex template instantiation in headers like `<vector>`, `<string>`, and `<algorithm>` takes 10+ seconds
2. **Require advanced features** - Allocators, exceptions, advanced constexpr, and other features not yet implemented
3. **Are for tracking progress** - They serve as goals for future development

## Running These Tests

To test standard header support, use the dedicated test script:

```bash
cd tests/standard_headers
./test_std_headers_comprehensive.sh
```

This script:
- Tests each standard header with a 10-second timeout
- Reports compilation success, timeout, or failure
- Shows first error message for failed tests
- Provides a summary of results

## Test Files

### C Standard Library Wrappers (Simpler)
- `test_cstddef.cpp` - Tests `<cstddef>` header
- `test_cstdio_puts.cpp` - Tests `<cstdio>` header
- `test_cstdlib.cpp` - Tests `<cstdlib>` header

### C++ Standard Library Headers (Complex)
All `test_std_*.cpp` files test their corresponding standard headers. See `STANDARD_HEADERS_MISSING_FEATURES.md` for details on why each fails.

## Documentation

- **`README_STANDARD_HEADERS.md`** - Overview of standard header tests and results
- **`STANDARD_HEADERS_MISSING_FEATURES.md`** - Comprehensive analysis of missing features blocking compilation
- **`test_real_std_headers_fail.cpp`** - Earlier analysis of header support issues

## Integration with Main Test Suite

When FlashCpp gains support for standard headers (through template optimization, allocator support, etc.), successfully compiling tests can be moved back to the main `tests/` directory to be included in the regular test suite.

## Adding New Standard Header Tests

1. Create `test_std_<header_name>.cpp` in this directory
2. Verify it's valid C++20 with `clang++ -std=c++20 -c test_std_<header_name>.cpp`
3. Test with FlashCpp using `./test_std_headers_comprehensive.sh`
4. Document any new findings in `STANDARD_HEADERS_MISSING_FEATURES.md`
