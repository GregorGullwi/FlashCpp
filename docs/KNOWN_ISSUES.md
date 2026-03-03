# Known Issues

_No active known issues at this time._

## Resolved

### Lambda unary-plus fallback return type

- **Status**: ~~Known deviation~~ → **Fixed** (2026-03-03)
- **Description**: When a captureless lambda with no explicit trailing return type and no return statements was decayed via unary `+` to a function pointer, the compiler fell back to `int` as the return type. This has been fixed to correctly deduce `void`, consistent with C++20 §7.5.5.1.
