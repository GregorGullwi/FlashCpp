# Future Test Files

This directory contains `_fail.cpp` test files that document **desired** compiler
behaviour which is not yet enforced by FlashCpp.

These tests are valid C++20 programs that a conforming compiler (e.g. clang, gcc)
**rejects**. This directory is reserved for valid C++20 programs that FlashCpp
still accepts because some compile-time-evaluation enforcement corner is not yet
covered by the normal `_fail.cpp` suite.

They are stored here (outside `tests/`) so that the CI test runners
(`run_all_tests.sh`, `test_reference_files.ps1`) do not pick them up as
`_fail.cpp` tests — which would cause spurious "UNEXPECTED PASS" failures.

Once the corresponding enforcement is implemented, move each file back to
`tests/` so it is tested normally.

## Previously here, now promoted to `tests/`

The following tests were moved to `tests/` once constexpr pointer violation
enforcement was implemented (ptr+ptr, OOB dereference, relational comparison
of different-array pointers now produce compile errors tagged
`EvalErrorType::NotConstantExpression`):

* `test_constexpr_ptr_arith_fail.cpp`
* `test_constexpr_ptr_diff_different_arrays_fail.cpp`
* `test_constexpr_ptr_negative_offset_fail.cpp`
* `test_constexpr_ptr_oob_deref_fail.cpp`
* `test_constexpr_ptr_relational_diff_arrays_fail.cpp`
