# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## `constexpr` variable enforcement for evaluator gaps

C++20 requires every `constexpr` variable initializer to be a constant expression.
FlashCpp now rejects genuine non-constant-expression cases such as calling a
non-`constexpr`/non-`consteval` function from a `constexpr` variable initializer,
but evaluator-gap failures that still surface as `EvalErrorType::Other` remain open.

Known remaining behavior:
* a `constexpr` variable whose initializer fails constant evaluation because the
  evaluator lacks support for a valid C++20 construct can still fall back to the
  old warning-and-zero-initialize path instead of producing the right constant value

Future task: keep converting genuine C++ violations to
`EvalErrorType::NotConstantExpression`, and add evaluator support for valid C++20
constructs that currently report `Other`.
