# C++20 Grammar Audit: Declaration Parsing & Feature Coverage

## Overview
This audit covers structural issues within the parser, specifically regarding declaration parsing, operator precedence, and C++20 feature coverage. It was originally written to document dispatch-routing bugs and has been updated (Feb 2026) with current status and broader feature analysis.

---

## Original Findings (Status Updated)

### 1. Unified vs. Split Declaration Parsing — MITIGATED

The parser maintains two separate declaration parsing paths:
*   `parse_declaration_or_function_definition`: Handles both functions and variables with full specifier support.
*   `parse_variable_declaration`: Originally a variable-only parser.

**Current status:** `parse_variable_declaration` now contains a fallback (Parser_Statements.cpp:531-627) that detects function declarations via `looks_like_function_parameters()` and delegates to `parse_function_declaration`. This means the two-path architecture still exists, but both paths produce correct results.

**Remaining concern:** The dispatch table in `parse_statement_or_declaration` (Parser_Statements.cpp:78-138) still routes type keywords like `int`, `float`, `char`, `bool` to `parse_variable_declaration`, while only `void` goes to `parse_declaration_or_function_definition`. The code is **functionally correct** but **architecturally suboptimal** — function declarations like `int main() {}` take an indirect path through variable parsing before being redirected.

### 2. The "Double Parse" / Misrouting Issue — MITIGATED

The dispatch asymmetry (`void` -> `parse_declaration_or_function_definition`, everything else -> `parse_variable_declaration`) still exists in the dispatch table. However, the fallback logic in `parse_variable_declaration` now correctly handles function declarations:
1.  Parses `int main`
2.  Sees `(`, calls `looks_like_function_parameters()`
3.  If true, delegates to `parse_function_declaration()` with full body support

**Result:** `int main() { ... }` and `static int func() { ... }` parse correctly. The original catastrophic misparse no longer occurs.

### 3. Redundant / Conflicting Keyword Parsing — FIXED

Both `parse_variable_declaration` and `parse_declaration_or_function_definition` now call the shared `parse_declaration_specifiers()` helper (Parser_Declarations.cpp:1579-1632) instead of maintaining their own inline loops. The helper returns a `DeclarationSpecifiers` struct (ParserTypes.h:135-152) with storage class, constexpr/consteval/constinit flags, inline, linkage, and calling convention.

A separate `parse_member_leading_specifiers()` handles struct/class member-specific specifiers (virtual, explicit, etc.) — this is intentionally different, not duplicative.

### 4. Operator Precedence (`<=>`) — FIXED

The spaceship operator `<=>` is now assigned precedence **14** in `get_operator_precedence` (Parser_Expressions.cpp:1665), correctly placed between shift operators (15) and relational operators (13). A comment on line 1656 confirms the standard order: `Shift > Three-Way (<=>)  > Relational`.

### 5. Range-based For Loops (Init-statement) — FIXED

`parse_for_loop` (Parser_Expressions.cpp:8823-9058) correctly handles all three forms:
*   Regular: `for (init; cond; inc)`
*   Range-based: `for (decl : expr)`
*   C++20 range-for with init: `for (init; decl : expr)` — uses save/restore position for lookahead

---

## New Findings (Feb 2026)

### 6. Coroutines — NOT IMPLEMENTED (High Severity)

`co_await`, `co_yield`, and `co_return` are **not recognized as keywords** in TokenTable.h, TokenKind.h, or Lexer.h. No parsing logic exists anywhere in the Parser_*.cpp files.

**Misleading macro:** `__cpp_impl_coroutine` is defined as `201902L` in FileReader_Macros.cpp:1459, which tells user code that coroutines are supported when they are not. Headers guarded by this macro (like `<coroutine>`) may appear to preprocess but will fail at the parsing stage.

**Impact:** Any code using coroutines will fail to parse.

### 7. Modules — NOT IMPLEMENTED (High Severity)

`import` and `module` are **not defined as keywords** anywhere in the lexer or token tables. No parsing logic exists for module declarations (`module foo;`), module imports (`import std;`), or module exports (`export module foo;`).

**Impact:** Any code using C++20 modules will fail to parse.

### 8. Template Lambdas — Parsed but Discarded (Medium Severity)

The parser recognizes the C++20 `[]<typename T>(T x) {}` syntax (Parser_Expressions.cpp:9446-9483) and parses the template parameter names. However, the parsed `template_param_names` vector is **never stored in the AST**. `LambdaExpressionNode` (AstNodeTypes.h:3529-3569) has no field for template parameters, and the node constructor doesn't accept them.

Simple cases work incidentally via auto-parameter deduction, but explicit template lambda instantiation or SFINAE within template lambdas will not work.

**Additional missing lambda specifiers:**
*   `noexcept` on lambdas — not parsed (jumps from `mutable` directly to `-> return_type`)
*   `constexpr`/`consteval` on lambdas — not parsed
*   `requires` clause on lambdas — not parsed

### 9. Aggregate Parenthesized Initialization — Not Properly Implemented (Medium Severity)

C++20 (P0960) allows aggregate types to be initialized with parentheses: `Point p(1, 2)` where `Point` has no constructors.

The feature macro `__cpp_aggregate_paren_init` is defined as `201902L` (FileReader_Macros.cpp:1429), but the actual implementation only handles brace initialization (`Type{args}`) at Parser_Expressions.cpp:7862. The parenthesized form `Type(args)` goes through `parse_direct_initialization()` which treats it as a constructor call, not aggregate init.

**Impact:** Aggregate paren init will fail or produce incorrect results for types without user-defined constructors.

### 10. `[[likely]]`/`[[unlikely]]` in switch/case — Partial (Medium Severity)

These attributes are correctly skipped in `if`/`else` contexts (Parser_Expressions.cpp:10131-10157). However, `parse_switch_statement()` (Parser_Expressions.cpp:10280-10409) does **not** call `skip_cpp_attributes()` after `case VALUE:` or `default:` labels.

C++20 allows:
```cpp
switch (x) {
    case 1: [[likely]] do_something(); break;
    case 2: [[unlikely]] do_other(); break;
}
```

This will fail to parse because `[[likely]]` is treated as an unexpected token after the colon.

### 11. Misleading Feature Macros (Low Severity)

Several `__cpp_*` feature-test macros advertise support for features that are not (fully) implemented:

| Macro | Value | Actual Status |
|-------|-------|---------------|
| `__cpp_impl_coroutine` | `201902L` | Not implemented at all |
| `__cpp_aggregate_paren_init` | `201902L` | Only brace form works, not parenthesized |

These can cause user code to take codepaths that the compiler cannot handle, producing confusing errors far from the actual limitation.

---

## Summary Table

| # | Issue | Severity | Status |
|---|-------|----------|--------|
| 1 | Split declaration parsing paths | Low | Mitigated — fallback works, routing still indirect |
| 2 | Dispatch misrouting (`int` vs `void`) | Low | Mitigated — fallback handles it correctly |
| 3 | Redundant specifier parsing | — | **Fixed** (shared `parse_declaration_specifiers`) |
| 4 | `<=>` operator precedence | — | **Fixed** (precedence 14, between shift and relational) |
| 5 | Range-for with init-statement | — | **Fixed** (full C++20 support) |
| 6 | Coroutines (`co_await`/`co_yield`/`co_return`) | **High** | Not implemented; misleading macro defined |
| 7 | Modules (`import`/`module`) | **High** | Not implemented |
| 8 | Template lambda params discarded | **Medium** | Parsed but not stored in AST |
| 9 | Aggregate parenthesized init | **Medium** | Feature macro set but not implemented for `()` form |
| 10 | `[[likely]]`/`[[unlikely]]` in switch/case | **Medium** | Only if/else handled, not switch/case |
| 11 | Misleading feature-test macros | Low | `__cpp_impl_coroutine`, `__cpp_aggregate_paren_init` |

## Recommended Actions

1.  **Dispatch cleanup (low priority):** Route all declaration-starting keywords to `parse_declaration_or_function_definition` to eliminate the indirect path through `parse_variable_declaration`. This is cleanup, not a correctness fix.

2.  **Coroutine keywords (high priority):** Add `co_await`, `co_yield`, `co_return` to the lexer/token tables and implement basic parsing. Alternatively, remove the `__cpp_impl_coroutine` macro until support is real.

3.  **Module keywords (high priority):** Add `import`, `module` to the lexer/token tables and implement basic parsing. This is a large feature and can be deferred, but the keywords should at least be recognized to produce a clear error.

4.  **Template lambda storage:** Add a `template_params_` field to `LambdaExpressionNode` and wire it through construction.

5.  **Lambda specifiers:** Add parsing for `noexcept`, `constexpr`/`consteval`, and `requires` on lambda expressions.

6.  **Switch/case attributes:** Add `skip_cpp_attributes()` call after case/default label colons in `parse_switch_statement()`.

7.  **Feature macro audit:** Remove or downgrade `__cpp_impl_coroutine` and `__cpp_aggregate_paren_init` until the features are actually implemented.
