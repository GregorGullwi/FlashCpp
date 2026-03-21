# Future Test Files

This directory contains `_fail.cpp` test files that document **desired** compiler
behaviour which is not yet enforced by FlashCpp.

These tests are valid C++20 programs that a conforming compiler (e.g. clang, gcc)
**rejects**. FlashCpp currently accepts them because `constexpr`/`consteval`
enforcement is not yet implemented (see `docs/KNOWN_ISSUES.md`).

They are stored here (outside `tests/`) so that the CI test runners
(`run_all_tests.sh`, `test_reference_files.ps1`) do not pick them up as
`_fail.cpp` tests — which would cause spurious "UNEXPECTED PASS" failures.

Once the corresponding enforcement is implemented, move each file back to
`tests/` so it is tested normally.
