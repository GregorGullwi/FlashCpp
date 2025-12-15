# Internal Tests for String Interning

This folder contains unit tests for the string interning infrastructure.

## Tests

- `string_table_standalone_test.cpp` - Standalone unit tests for StringTable (recommended)
- `string_table_test.cpp` - Legacy test file (depends on full parser)

## Running Tests

The standalone test should be run before each commit in the string interning PR:

```bash
# Compile and run standalone test
cd /home/runner/work/FlashCpp/FlashCpp
clang++ -std=c++20 -I src tests/internal/string_table_standalone_test.cpp -o /tmp/string_table_test
/tmp/string_table_test
```

Expected output:
```
=== StringTable Unit Tests ===
Test: StringHandle creation and round-trip... PASSED
Test: String interning deduplication... PASSED
Test: Hash consistency... PASSED
Test: Empty string handling... PASSED
Test: Long string handling... PASSED
Test: Special characters... PASSED
Test: StringHandle as map key... PASSED
Test: Allocation across chunk boundary... PASSED
=== All tests PASSED ===
```

## Test Coverage

The tests verify:
- String handle creation and round-trip
- Interning and deduplication
- Hash consistency
- Edge cases (empty strings, long strings, special characters)
- StringHandle as map key
- Chunk boundary allocation
