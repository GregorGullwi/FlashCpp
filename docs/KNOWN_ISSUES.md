# Known Issues

## Lambda unary-plus fallback return type

- **Status**: Known deviation (2026-03-03)
- **Description**: When a captureless lambda with no explicit trailing return type and no return statements is decayed via unary `+` to a function pointer, the compiler falls back to `int` as the return type. This preserves prior behavior but differs from C++20, where such lambdas would deduce `void`.
- **Impact**: The resulting function pointer may have a return type of `int` instead of `void`, which can affect type-checking or overload resolution.
- **Planned follow-up**: Align the decay return type with the standard by deducing `void` when no return expressions are present.
