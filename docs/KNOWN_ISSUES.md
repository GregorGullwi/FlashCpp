# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Parser recursion/iteration limits on deeply nested expressions

- Extremely deep unary-expression nesting can still overflow the parser stack and crash
  the compiler before semantic analysis/constexpr evaluation runs.
- Very long flat binary-expression chains can hit `MAX_BINARY_OP_ITERATIONS` in
  `parse_expression`, which currently surfaces as a parse failure instead of a more
  graceful depth/complexity diagnostic.
- This was observed while trying to build a deep `noexcept(...)` stress test: the new
  constexpr evaluator recursion guard works, but the parser can still fail first on
  sufficiently deep source expressions.

## Template-struct member operator lookup gaps

- Some member operator overloads on class-template instantiations still fail to resolve in
  expressions like `lhs + rhs`, reporting `Operator+ not defined for operand types`.
- The same family of failures was also observed while probing function-template bodies that
  operate on template-struct operands.
- This was discovered while extending operator-overload follow-up regressions on 2026-03-11.
