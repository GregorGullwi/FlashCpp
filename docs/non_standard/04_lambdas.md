# FlashCpp Non-Standard Behavior — Lambdas

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/Parser_Expr_ControlFlowStmt.cpp`, `src/IrGenerator_Lambdas.cpp`.
For constexpr-specific lambda limitations see [05_constexpr.md](05_constexpr.md).

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 2.1 Capture-All `[=]` / `[&]` Expanded at Parse Time, Not by ODR-Use ❌

**Standard (C++20 [expr.prim.lambda.capture]):** The set of entities captured by `[=]` or
`[&]` is exactly the set of *odr-used* local variables and parameters within the lambda body.
This set is a semantic property and cannot be determined before the body is fully analysed.

**FlashCpp:** `parse_lambda_expression` (Parser_Expr_ControlFlowStmt.cpp:1036–1114) eagerly
enumerates every symbol currently visible in the symbol table at the point of the `[` token
and converts all of them into explicit per-variable captures. Consequences:

- Variables that are in scope but never used inside the body are captured unnecessarily.
- Variables declared after the lambda but in the same enclosing scope are never captured.
- Variables that are only odr-used inside a discarded branch may be incorrectly captured.

**Location:** `src/Parser_Expr_ControlFlowStmt.cpp:1036–1114`

---
