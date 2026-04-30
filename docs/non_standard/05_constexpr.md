# FlashCpp Non-Standard Behavior — Constant Expressions (`constexpr` / `consteval`)

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/ConstExprEvaluator_Core.cpp`, `src/ConstExprEvaluator_Members.cpp`.
See also `docs/CONSTEXPR_LIMITATIONS.md` for a more detailed treatment of items marked **[Known]**.

**Scope note:** this document describes FlashCpp behavior against **C++20 constexpr**.
It does **not** claim support for constexpr exception handling. Any mention of
internal `catch` behavior below refers to evaluator implementation/diagnostics,
not to `throw` / `try` / `catch` being supported during constant evaluation.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 4.4 `sizeof(arr)` May Be Parsed as `sizeof(type)` in Some Contexts ⚠️

**Standard:** `sizeof(arr)` where `arr` is an array variable must be treated as
`sizeof(expression)`, yielding the full array size.

**FlashCpp:** In some paths the parser still incorrectly treats `arr` as a type name. The
current workaround detects placeholder type nodes whose `size_in_bits()` is zero and then
re-looks up the identifier in the symbol table to recover the array object.

**Location:** `src/ConstExprEvaluator_Core.cpp:1202–1209`, `src/IrGenerator_MemberAccess.cpp:1935–1942`

---

### 2.3 `[this]` and `[*this]` Constexpr Lambda Capture Is Partial ⚠️

**Standard (C++20 [expr.const]):** A `constexpr` lambda in a `constexpr` member function may
capture `this` or `*this`.

**FlashCpp:** Basic supported shapes now work, including simple member reads/calls through
explicit `this` capture, straightforward mutable shared-object updates through `[this]`, and
straightforward mutable `[*this]` copy-local updates. However, constexpr evaluation still
rejects implicit capture-default lambdas outright:

> `Implicit capture [=] or [&] not supported in constexpr lambdas - use explicit captures`

So support for `this`/`*this` capture is materially better than before, but constexpr lambda
capture semantics are still incomplete.

**Location:** `src/ConstExprEvaluator_Core.cpp:3127–3135`, `src/ConstExprEvaluator_Members.cpp`, `docs/CONSTEXPR_LIMITATIONS.md:254–282`
