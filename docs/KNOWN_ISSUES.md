# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Runtime calls on some lambda objects can fail to link with an unresolved generated
  `operator()` symbol (observed while testing `test_constexpr_lambda_multistmt_ret0.cpp`).
