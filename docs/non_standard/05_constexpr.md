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

### 4.1 Constructor Body Assignments in `constexpr` Are Supported ✅

**Standard (C++20 [expr.const]):** A `constexpr` constructor may initialise members via
assignments in the constructor body.

**FlashCpp:** Straightforward constructor-body member assignments now evaluate in
constant expressions, including current supported `if` / `else`, `for`, `while`,
and `switch` bodies.

**Location:** `docs/CONSTEXPR_LIMITATIONS.md` ("Implemented ✅")

---

### 4.2 Multi-Statement `constexpr` Functions Are Evaluated ✅

**Standard (C++20 [dcl.constexpr]):** A `constexpr` function may contain conditionals, loops,
local variables, and multiple return paths.

**FlashCpp:** Multi-statement `constexpr` free functions, member functions, and
current supported lambda/callable bodies now evaluate at compile time.

**Location:** `docs/CONSTEXPR_LIMITATIONS.md` ("Implemented ✅")

---

### 4.3 `consteval` Compile-Time-Only Enforcement Works ✅

**Standard (C++20 [dcl.consteval]):** Calling a `consteval` function outside a
constant-expression context is ill-formed and must produce a compile error.

**FlashCpp:** `consteval` calls are enforced as immediate invocations. Non-constant
runtime-argument calls are rejected, and current regression coverage includes free
functions, member functions, namespace-scope functions, templates, and aggregate-returning
calls.

**Location:** `tests/test_consteval_basic_ret0.cpp`, `tests/test_consteval_template_ret0.cpp`,
`tests/test_consteval_member_runtime_arg_fail.cpp`, `tests/test_consteval_runtime_arg_fail.cpp`

---

### 4.4 `sizeof(arr)` May Be Parsed as `sizeof(type)` in Some Contexts ⚠️

**Standard:** `sizeof(arr)` where `arr` is an array variable must be treated as
`sizeof(expression)`, yielding the full array size.

**FlashCpp:** In some paths the parser incorrectly treats `arr` as a type name. Two
workarounds exist that detect `size_in_bits == 0` and fall back to a symbol table lookup:

**Location:** `src/ConstExprEvaluator_Core.cpp:209–230`, `src/IrGenerator_MemberAccess.cpp:1441`

---

### 4.5 Many Expression Types Return a Runtime Error Instead of a Compile-Time Error ⚠️

**Standard:** If a `constexpr` expression cannot be evaluated at compile time for a required
constant-expression context, the program is ill-formed and the compiler must diagnose it.

**FlashCpp:** `ConstExprEvaluator_Core.cpp:153–154` has an internal catch-all that returns
`EvalResult::error(…)` at *runtime*, which causes the caller to silently treat the expression
as non-constant and fall back to a codegen path that may produce wrong code without any
visible diagnostic. Other specific gaps: `sizeof` with complex expressions (line 533),
certain bitwise and shift operators (lines 594–623).

This is a diagnostics/implementation issue, **not** support for constexpr
exception handling.

**Location:** `src/ConstExprEvaluator_Core.cpp:153–154, 533, 594–623`

---

### 4.6 Cast-Result Bases in Nested Member Access Are Supported in Current Shapes ✅

**Standard:** Arbitrary nesting depth of member access (`a.b.c.d`, including function-call
result bases) is valid in constant expressions.

**FlashCpp:** Simple `a.b.c` chains, constexpr function-result bases, and
`static_cast` / cv-only `const_cast` result bases now work in current supported shapes.

**Location:** `src/ConstExprEvaluator_Members.cpp`

---

### 2.2 `[=]` / `[&]` Constexpr Lambda Capture Is Supported in Current Shapes ✅

**Standard (C++20 [expr.const]):** A `constexpr` lambda may use implicit captures as long as
all odr-used captured entities satisfy constant-expression constraints.

**FlashCpp:** Capture-all lambdas are expanded during parsing into concrete captures in the
supported cases. Existing constexpr regressions cover local default captures and default member
captures that imply `this` in supported shapes.

**Location:** `src/Parser_Expr_ControlFlowStmt.cpp`, `docs/CONSTEXPR_LIMITATIONS.md:254–282`

---

### 2.3 `[this]` and `[*this]` Constexpr Lambda Capture Is Partial ⚠️

**Standard (C++20 [expr.const]):** A `constexpr` lambda in a `constexpr` member function may
capture `this` or `*this`.

**FlashCpp:** Basic supported shapes now work, including simple member reads/calls through
explicit or implicit `this` capture, straightforward mutable shared-object updates through
`[this]`, and straightforward mutable `[*this]` copy-local updates. Richer captured-object
aliasing/identity behavior still remains partial.

**Location:** `src/ConstExprEvaluator_Core.cpp`, `src/ConstExprEvaluator_Members.cpp`, `docs/CONSTEXPR_LIMITATIONS.md:254–282`

---

### 3.3 `std::integral_constant::value` Synthesised From Template Arguments, Bypassing Lookup ⚠️

**Standard:** `std::integral_constant<T,v>::value` is a well-defined static `constexpr` data
member accessed through normal name lookup and constexpr evaluation.

**FlashCpp:** `ConstExprEvaluator_Members.cpp:905` synthesises the value directly from the
template argument when the static member is not registered, labelled as:

> *"Fallback: synthesize integral_constant::value from template arguments when static member
> isn't registered"*

This gives the correct result for the primary template but would give wrong answers for any
explicit specialisation of `integral_constant`.

**Location:** `src/ConstExprEvaluator_Members.cpp:905`
