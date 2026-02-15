# C++20 Grammar Audit: Declaration Parsing & Feature Coverage

## Overview
This audit covers structural issues within the parser, specifically regarding declaration parsing and C++20 feature coverage. Updated Feb 2026.

---

## Mitigated: Declaration Dispatch Routing

The parser has two separate declaration parsing paths:
*   `parse_declaration_or_function_definition`: Handles both functions and variables with full specifier support.
*   `parse_variable_declaration`: Originally a variable-only parser, now contains a fallback (Parser_Statements.cpp:531-627) that detects function declarations via `looks_like_function_parameters()` and delegates to `parse_function_declaration`.

The dispatch table in `parse_statement_or_declaration` (Parser_Statements.cpp:78-138) routes all type keywords (`int`, `float`, `char`, `bool`, storage class specifiers, etc.) to `parse_variable_declaration`, with only `void` going to `parse_declaration_or_function_definition`. The fallback means both paths produce correct results, but function declarations like `int main() {}` take an indirect path through variable parsing before being redirected.

### Long-Term Resolution Plan

**Goal:** Eliminate the indirect dispatch path so all declarations route through `parse_declaration_or_function_definition`.

**Phase 1 — Reroute the dispatch table:**
1. In `parse_statement_or_declaration` (Parser_Statements.cpp:98-128), change all entries currently pointing to `parse_variable_declaration` to point to `parse_declaration_or_function_definition` instead. This covers ~30 keyword entries (`static`, `extern`, `int`, `float`, `char`, `bool`, `const`, `auto`, `decltype`, `__int64`, etc.).
2. Verify `parse_declaration_or_function_definition` already handles all cases these keywords can introduce (it should, since `parse_variable_declaration` delegates to it for functions and `parse_declaration_or_function_definition` already falls back to variable parsing).
3. Run the full test suite via `tests/run_all_tests.sh` to catch regressions.

**Phase 2 — Demote `parse_variable_declaration` to internal helper:**
1. Remove `parse_variable_declaration` from the dispatch table entirely (no keyword should map to it directly).
2. Keep `parse_variable_declaration` as a helper called only from within `parse_declaration_or_function_definition` as its variable-declaration fallback path.
3. Remove the function-detection fallback (`looks_like_function_parameters()` branch at lines 531-627) from `parse_variable_declaration`, since it will no longer be reached directly for function declarations.
4. Audit the other call sites in `parse_statement_or_declaration` (lines 271, 338, 348, 382) that call `parse_variable_declaration` directly for identifier-led declarations — these may need to route through `parse_declaration_or_function_definition` instead.

**Phase 3 — Cleanup:**
1. Remove the `void` special case from the dispatch table (it will follow the same path as all other types).
2. Verify no external callers remain for `parse_variable_declaration` besides `parse_declaration_or_function_definition`.

**Risk:** Low. The fallback already works correctly, so this is a code-quality refactor. Each phase can be tested independently. If Phase 1 introduces regressions, the entries can be reverted individually.

---

## Open Issues

### 1. Template Lambdas — Parsed but Discarded (Medium Severity)

The parser recognizes the C++20 `[]<typename T>(T x) {}` syntax (Parser_Expressions.cpp:9446-9483) and parses the template parameter names. However, the parsed `template_param_names` vector is **never stored in the AST**. `LambdaExpressionNode` (AstNodeTypes.h:3529-3569) has no field for template parameters, and the node constructor doesn't accept them.

Simple cases work incidentally via auto-parameter deduction, but explicit template lambda instantiation or SFINAE within template lambdas will not work.

**Additional missing lambda specifiers:**
*   `noexcept` on lambdas — not parsed (jumps from `mutable` directly to `-> return_type`)
*   `constexpr`/`consteval` on lambdas — not parsed
*   `requires` clause on lambdas — not parsed

### 2. Aggregate Parenthesized Initialization — Fixed

C++20 (P0960) allows aggregate types to be initialized with parentheses: `Point p(1, 2)` where `Point` has no constructors.

**Resolution:** Added aggregate init fallback to the declaration-context codegen path (CodeGen_Statements.cpp). When no matching user-defined constructor is found and the struct is an aggregate, direct member stores are generated instead of a constructor call. This matches the existing aggregate init logic in the expression-context path (CodeGen_Lambdas.cpp). Works for block scope direct init, global scope, copy-init from temporaries, and expression context.

### 3. `[[likely]]`/`[[unlikely]]` in switch/case — Partial (Medium Severity)

These attributes are correctly skipped in `if`/`else` contexts (Parser_Expressions.cpp:10131-10157). However, `parse_switch_statement()` (Parser_Expressions.cpp:10280-10409) does **not** call `skip_cpp_attributes()` after `case VALUE:` or `default:` labels.

C++20 allows:
```cpp
switch (x) {
    case 1: [[likely]] do_something(); break;
    case 2: [[unlikely]] do_other(); break;
}
```

This will fail to parse because `[[likely]]` is treated as an unexpected token after the colon.

### 4. Misleading Feature Macros (Low Severity)

Several `__cpp_*` feature-test macros advertise support for features that are not (fully) implemented:

| Macro | Value | Actual Status |
|-------|-------|---------------|
| `__cpp_impl_coroutine` | `201902L` | Not planned (see below) — **macro removed** |
| `__cpp_aggregate_paren_init` | `201902L` | **Fixed** — both `()` and `{}` forms work |

`__cpp_impl_coroutine` has been removed. `__cpp_aggregate_paren_init` is now accurately defined.

---

## Summary Table

| # | Issue | Severity | Status |
|---|-------|----------|--------|
| — | Declaration dispatch routing | Low | **Fixed** — keyword dispatch unified |
| 1 | Template lambda params discarded | **Medium** | **Fixed** — stored in AST, specifiers parsed |
| 2 | Aggregate parenthesized init | **Medium** | **Fixed** — both `()` and `{}` forms work |
| 3 | `[[likely]]`/`[[unlikely]]` in switch/case | **Medium** | **Fixed** — skip_cpp_attributes() added |
| 4 | Misleading feature-test macros | Low | **Fixed** — `__cpp_impl_coroutine` removed, `__cpp_aggregate_paren_init` now accurate |

---

## Not Planned Features

These C++20 features are intentionally out of scope for FlashCpp.

### Coroutines

`co_await`, `co_yield`, and `co_return` are not recognized as keywords and no parsing logic exists. The `__cpp_impl_coroutine` macro is currently defined as `201902L` in FileReader_Macros.cpp:1459, which should be removed to avoid misleading user code into coroutine codepaths.

### Modules

`import` and `module` are not defined as keywords anywhere in the lexer or token tables. No parsing logic exists for module declarations, imports, or exports.

---

## Recommended Actions

1.  **Dispatch refactor:** Follow the phased plan above to unify declaration routing.

2.  **Template lambda storage:** Add a `template_params_` field to `LambdaExpressionNode` and wire it through construction.

3.  **Lambda specifiers:** Add parsing for `noexcept`, `constexpr`/`consteval`, and `requires` on lambda expressions.

4.  **Switch/case attributes:** Add `skip_cpp_attributes()` call after case/default label colons in `parse_switch_statement()`.

5.  **Feature macro audit:** Remove `__cpp_impl_coroutine` (not planned). Remove or downgrade `__cpp_aggregate_paren_init` until parenthesized aggregate init is implemented.
