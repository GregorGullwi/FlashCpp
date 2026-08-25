# Future Test Files

This directory contains inactive source files that document desired rejection
behavior which FlashCpp does not yet enforce.

These tests are valid C++20 programs that a conforming compiler (e.g. clang, gcc)
**rejects**. This directory is reserved for valid C++20 programs that FlashCpp
still accepts because some compile-time-evaluation enforcement corner is not yet
covered by the active negative-test suite.

They are stored here (outside `tests/`) so that the CI test runners
(`run_all_tests.sh`, `run_all_tests.ps1`) do not pick them up as active
negative tests.

Once the corresponding enforcement is implemented, assign a stable shared
`DiagnosticId`, move the file back to `tests/`, and rename it with terminal
`_e<number>` segments encoding the exact emitted ID multiset. Do not add these
names to the frozen legacy `_fail.cpp` inventory.

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
