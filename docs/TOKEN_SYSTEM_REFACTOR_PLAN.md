# Token System Refactor Proposal

## Problem Summary
- `Parser::peek_token()` returns `std::optional<Token>`, which forces repetitive `has_value()` checks and encourages string comparisons such as `peek_token()->value() == "{"`.
- Alternate spellings (`||` vs `or`, `&&` vs `and`, etc.) require manual normalization at call sites instead of being folded into a single token identity.
- Error reporting still needs full token metadata (line/column/file), but most hot paths only need a lightweight discriminator.

## Goals
- Keep a cheap, always-present way to ask “what token is next?” without optional wrappers or string compares.
- Preserve a path to full token metadata for diagnostics and AST nodes.
- Canonicalize equivalent spellings through a user-defined literal (`"_tok"`) to keep call sites readable.
- Make the migration incremental so parser code can be updated in small steps.

## Proposed Shape

### Token identity
- Introduce `enum class TokenKind : uint16_t` that captures the canonical identity of each token:
  - structural: `OpenBrace`, `CloseBrace`, `OpenParen`, `CloseParen`, `Comma`, `Semicolon`, `Colon`, `Scope`, `TemplateOpen`, `TemplateClose`, etc.
  - operators: `LogicalOr`, `LogicalAnd`, `BitOr`, `BitAnd`, `Plus`, `Minus`, `Star`, `Slash`, `Percent`, `Equal`, `EqualEqual`, `NotEqual`, `Less`, `Greater`, `LessEqual`, `GreaterEqual`, `ShiftLeft`, `ShiftRight`, etc.
  - keywords: `If`, `Else`, `Template`, `Requires`, `Return`, `Class`, `Struct`, `Union`, `Enum`, `Typename`, `Using`, `Operator`, `Constexpr`, `Noexcept`, `Alignas`, `Alignof`, `Concept`, etc.
  - categories: `Identifier`, `NumericLiteral`, `StringLiteral`, `CharLiteral`, `EndOfFile`, `Unknown`.

### Lightweight view vs. full token
- Add a `TokenView` (or similar) that is trivially copyable and always available:
  - fields: `TokenKind kind; std::string_view lexeme; bool isEof()` helper that checks `kind == TokenKind::EndOfFile`.
  - no ownership or file/line data; meant for fast branching.
- Retain `Token` for full metadata (line, column, file index) and payload.
- Keep both lined up by storing `TokenKind` inside `Token` so conversion from `Token` to `TokenView` is a zero-copy reference.

### Accessor API
- Replace the optional-returning accessors with two clear entry points:
  - `TokenView Parser::peekKind(size_t lookahead = 0);` — cheap, never optional, returns `TokenKind::EndOfFile` sentinel.
  - `const Token& Parser::peekToken(size_t lookahead = 0);` — full metadata for diagnostics and AST storage.
- `consumeToken()` continues to advance and maintains both `current_token_` and a cached `TokenView` so repeated lookups avoid re-parsing.
- Existing loops become `while (peekKind().kind != TokenKind::EndOfFile) { ... }` and conditionals become `if (peekKind() == "{"_tok) { ... }`.

### `_tok` user-defined literal
- Provide `constexpr TokenKind operator""_tok(const char*, size_t)` that maps textual spellings to canonical kinds:
  - `"{“_tok -> TokenKind::OpenBrace`, `"}"_tok -> TokenKind::CloseBrace`, `"("_tok -> TokenKind::OpenParen`, `")"_tok -> TokenKind::CloseParen`, `","_tok -> TokenKind::Comma`, etc.
  - normalize alternate tokens: `"||"_tok` and `"or"_tok -> `TokenKind::LogicalOr`; `"&&"_tok` and `"and"_tok -> `TokenKind::LogicalAnd`; `"!"_tok` and `"not"_tok -> `TokenKind::Not`; `"~"_tok` and `"compl"_tok -> `TokenKind::BitNot`; `"|"`/`"bitor"`, `"&"`/`"bitand"`, `"^"`/`"xor"` get paired; `"^="`/`"xor_eq"` etc.
  - keyword spellings map directly (e.g., `"template"_tok -> TokenKind::Template`).
- Parsing code can stay readable (`if (peekKind() == "requires"_tok)`) without string-view equality checks.

## Migration Plan
1. **Introduce primitives**: add `TokenKind`, `_tok` literal, and `TokenView`. Keep `Token` unchanged except for an added `TokenKind` field.
2. **Wire lexer output**: emit `TokenKind` alongside lexeme when constructing `Token`; provide a helper to derive a `TokenView` from `Token` without allocation.
3. **Add new accessors**: implement `peekKind()`/`peekToken()` and adapt `consumeToken()` to maintain both forms. Keep the old `peek_token()` temporarily as a thin wrapper to minimize churn.
4. **Incremental parser migration**: convert hot-path comparisons (`value() == "..."`) to `peekKind() == "...“_tok` in small batches, prioritizing expression/statement parsing. Remove `std::optional` usage once callers stop depending on it.
5. **Clean-up phase**: delete legacy `peek_token()`/`std::optional` plumbing, collapse EOF handling to the `TokenKind::EndOfFile` sentinel, and simplify helpers that currently return `std::optional<Token>`.

## Testing Strategy
- Add focused lexer tests that assert `TokenKind` normalization (`||` vs `or`, `&&` vs `and`, `^=` vs `xor_eq`, etc.).
- Parser smoke tests that exercise representative control-flow, declaration, and template constructs using alternate spellings, verifying the same AST/IR is produced.
- Ensure existing regression suite still passes; pay attention to EOF-sensitive loops that previously relied on `std::optional` semantics.
