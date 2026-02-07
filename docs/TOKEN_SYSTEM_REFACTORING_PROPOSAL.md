# Token System Refactoring Proposal

## Motivation

The current token system wraps every `peek_token()` / `consume_token()` return in
`std::optional<Token>`.  That makes the most common parser operation — "is the
next token X?" — unreasonably verbose:

```cpp
// Current: 78 characters of boilerplate per check
if (peek_token().has_value() && peek_token()->value() == "{") {

// Proposed: 22 characters
if (peek() == "{"_tok) {
```

There are **~460** `peek_token().has_value() && peek_token()->value() == "..."`
sites, **~250** type-only checks, and **~90** combined type+value checks across
the six Parser source files.  Every one of them pays for three virtual
calls to `peek_token()` (the method is not inlined) plus the optional
unwrap.

## Design Goals

| # | Goal | Rationale |
|---|------|-----------|
| 1 | Replace the `optional`-everywhere API with a lightweight "token identity" value that is always valid | Removes 700+ `.has_value()` guards |
| 2 | Support ergonomic comparison via a user-defined literal `"..."_tok` | Keeps string readability (`"{"`, `"||"`) while mapping alternative spellings (`"or"` / `"||"`) to the same identity |
| 3 | Keep full location info (line, column, file_index) accessible on demand | Error messages and diagnostics still need it |
| 4 | Zero overhead: the lightweight type must fit in a register or two | No heap, no string compare on the hot path |
| 5 | Minimal churn: migrate incrementally, one file at a time | The optional API can coexist during migration |

## Core Concepts

### TokenKind — the lightweight identity

A `TokenKind` is a small value type (≤ 8 bytes) that uniquely identifies a
token's *semantic identity*.  Two tokens that mean the same thing in the
language produce the same `TokenKind`, regardless of spelling.

```cpp
// TokenKind.h
#include <cstdint>
#include <string_view>

class TokenKind {
public:
	// Categories (upper 8 bits)
	enum class Category : uint8_t {
		None = 0,       // EOF / uninitialized
		Identifier,     // user identifiers
		Keyword,        // language keywords
		Literal,        // numeric literals
		StringLiteral,
		CharLiteral,
		Operator,       // + - * / == != || && ...
		Punctuator,     // { } ( ) [ ] ; , : :: ...
	};

	constexpr TokenKind() = default;

	constexpr TokenKind(Category cat, uint16_t id)
		: category_(cat), id_(id) {}

	constexpr Category category()  const { return category_; }
	constexpr uint16_t id()        const { return id_; }

	constexpr bool operator==(TokenKind o) const {
		return category_ == o.category_ && id_ == o.id_;
	}
	constexpr bool operator!=(TokenKind o) const { return !(*this == o); }

	// Special sentinels
	static constexpr TokenKind eof()   { return {}; }
	static constexpr TokenKind ident() {
		return { Category::Identifier, 0 };
	}

	constexpr bool is_eof()        const { return category_ == Category::None; }
	constexpr bool is_identifier() const { return category_ == Category::Identifier; }
	constexpr bool is_keyword()    const { return category_ == Category::Keyword; }
	constexpr bool is_literal()    const { return category_ == Category::Literal; }
	constexpr bool is_operator()   const { return category_ == Category::Operator; }
	constexpr bool is_punctuator() const { return category_ == Category::Punctuator; }

private:
	Category category_ = Category::None;
	uint16_t id_ = 0;       // unique within category
	// Total: 4 bytes — fits in a single register
};
```

Every fixed token string (`"{"`, `"+="`, `"return"`, `"||"`, …) gets a
compile-time `constexpr TokenKind` constant.  Identifiers all share the
sentinel `TokenKind::ident()` (they are further distinguished by their
string value through full `TokenInfo`).

### The `"..."_tok` user-defined literal

```cpp
// TokenKind.h  (continued)

// Constexpr lookup: maps a string literal to a TokenKind.
// Implemented as a constexpr function with an if-else chain
// (compilers fold this to a constant at -O1).
consteval TokenKind operator""_tok(const char* s, size_t len) {
	std::string_view sv(s, len);

	// ---- Punctuators ----
	if (sv == "{")   return tok::LBrace;
	if (sv == "}")   return tok::RBrace;
	if (sv == "(")   return tok::LParen;
	if (sv == ")")   return tok::RParen;
	if (sv == "[")   return tok::LBracket;
	if (sv == "]")   return tok::RBracket;
	if (sv == ";")   return tok::Semi;
	if (sv == ",")   return tok::Comma;
	if (sv == ":")   return tok::Colon;
	if (sv == "::")  return tok::ColonColon;
	if (sv == "...")  return tok::Ellipsis;
	if (sv == ".")   return tok::Dot;
	if (sv == "->")  return tok::Arrow;
	if (sv == "#")   return tok::Hash;

	// ---- Operators ----
	if (sv == "+")   return tok::Plus;
	if (sv == "-")   return tok::Minus;
	if (sv == "*")   return tok::Star;
	if (sv == "/")   return tok::Slash;
	if (sv == "%")   return tok::Percent;
	if (sv == "=")   return tok::Assign;
	if (sv == "==")  return tok::EqEq;
	if (sv == "!=")  return tok::NotEq;
	if (sv == "<")   return tok::Less;
	if (sv == ">")   return tok::Greater;
	if (sv == "<=")  return tok::LessEq;
	if (sv == ">=")  return tok::GreaterEq;
	if (sv == "<=>") return tok::Spaceship;

	// --- Logical / bitwise with alternative token mappings ---
	if (sv == "&&" || sv == "and")     return tok::AmpAmp;
	if (sv == "||" || sv == "or")      return tok::PipePipe;
	if (sv == "!"  || sv == "not")     return tok::Exclaim;
	if (sv == "&"  || sv == "bitand")  return tok::Amp;
	if (sv == "|"  || sv == "bitor")   return tok::Pipe;
	if (sv == "^"  || sv == "xor")     return tok::Caret;
	if (sv == "~"  || sv == "compl")   return tok::Tilde;
	if (sv == "!=" || sv == "not_eq")  return tok::NotEq;
	if (sv == "&=" || sv == "and_eq")  return tok::AmpEq;
	if (sv == "|=" || sv == "or_eq")   return tok::PipeEq;
	if (sv == "^=" || sv == "xor_eq")  return tok::CaretEq;

	if (sv == "+=")  return tok::PlusEq;
	if (sv == "-=")  return tok::MinusEq;
	if (sv == "*=")  return tok::StarEq;
	if (sv == "/=")  return tok::SlashEq;
	if (sv == "%=")  return tok::PercentEq;
	if (sv == "<<")  return tok::LessLess;
	if (sv == ">>")  return tok::GreaterGreater;
	if (sv == "<<=") return tok::LessLessEq;
	if (sv == ">>=") return tok::GreaterGreaterEq;
	if (sv == "++")  return tok::PlusPlus;
	if (sv == "--")  return tok::MinusMinus;
	if (sv == "?")   return tok::Question;
	if (sv == ".*")  return tok::DotStar;
	if (sv == "->*") return tok::ArrowStar;

	// ---- Keywords (partial, most common ones) ----
	if (sv == "if")        return tok::KW_if;
	if (sv == "else")      return tok::KW_else;
	if (sv == "while")     return tok::KW_while;
	if (sv == "for")       return tok::KW_for;
	if (sv == "do")        return tok::KW_do;
	if (sv == "return")    return tok::KW_return;
	if (sv == "class")     return tok::KW_class;
	if (sv == "struct")    return tok::KW_struct;
	if (sv == "enum")      return tok::KW_enum;
	if (sv == "union")     return tok::KW_union;
	if (sv == "namespace") return tok::KW_namespace;
	if (sv == "template")  return tok::KW_template;
	if (sv == "typename")  return tok::KW_typename;
	if (sv == "typedef")   return tok::KW_typedef;
	if (sv == "using")     return tok::KW_using;
	if (sv == "const")     return tok::KW_const;
	if (sv == "static")    return tok::KW_static;
	if (sv == "virtual")   return tok::KW_virtual;
	if (sv == "override")  return tok::KW_override;
	if (sv == "final")     return tok::KW_final;
	if (sv == "public")    return tok::KW_public;
	if (sv == "private")   return tok::KW_private;
	if (sv == "protected") return tok::KW_protected;
	if (sv == "friend")    return tok::KW_friend;
	if (sv == "void")      return tok::KW_void;
	if (sv == "int")       return tok::KW_int;
	if (sv == "auto")      return tok::KW_auto;
	if (sv == "switch")    return tok::KW_switch;
	if (sv == "case")      return tok::KW_case;
	if (sv == "default")   return tok::KW_default;
	if (sv == "break")     return tok::KW_break;
	if (sv == "continue")  return tok::KW_continue;
	if (sv == "new")       return tok::KW_new;
	if (sv == "delete")    return tok::KW_delete;
	if (sv == "try")       return tok::KW_try;
	if (sv == "catch")     return tok::KW_catch;
	if (sv == "throw")     return tok::KW_throw;
	if (sv == "sizeof")    return tok::KW_sizeof;
	if (sv == "constexpr") return tok::KW_constexpr;
	if (sv == "static_cast")      return tok::KW_static_cast;
	if (sv == "dynamic_cast")     return tok::KW_dynamic_cast;
	if (sv == "const_cast")       return tok::KW_const_cast;
	if (sv == "reinterpret_cast") return tok::KW_reinterpret_cast;
	// ... remaining keywords added as needed

	// Fail at compile time if the literal is unrecognized
	throw "unrecognized token literal";
}
```

Key properties:

* `consteval` — the lookup runs entirely at compile time; the call site
  becomes a constant `TokenKind` value.
* `"||"_tok == "or"_tok` — alternative token spellings resolve to the
  same identity, so the parser can be written with whichever is most
  readable.
* An unrecognized string is a **compile error**, not a runtime surprise.
* No overhead at runtime — the comparison is just two integer compares.

### TokenInfo — full token data (replaces `Token`)

```cpp
// TokenInfo.h
#include "TokenKind.h"
#include <string_view>

class TokenInfo {
public:
	TokenInfo() = default;
	TokenInfo(TokenKind kind, std::string_view spelling,
	          size_t line, size_t column, size_t file_index)
		: kind_(kind), spelling_(spelling),
		  line_(line), column_(column), file_index_(file_index) {}

	TokenKind        kind()       const { return kind_; }
	std::string_view spelling()   const { return spelling_; }
	size_t           line()       const { return line_; }
	size_t           column()     const { return column_; }
	size_t           file_index() const { return file_index_; }

	// Convenience: compare directly with TokenKind
	bool operator==(TokenKind k) const { return kind_ == k; }
	bool operator!=(TokenKind k) const { return kind_ != k; }

private:
	TokenKind        kind_;
	std::string_view spelling_;
	size_t           line_   = 0;
	size_t           column_ = 0;
	size_t           file_index_ = 0;
};
```

`TokenInfo` is the full replacement for the old `Token` class. It carries
location info and the original spelling, but can be compared cheaply via
its `TokenKind`.

## New Parser API

### peek() and advance()

```cpp
class Parser {
	// ...

	// Returns the TokenKind of the current token.
	// Returns TokenKind::eof() when there are no more tokens.
	// Never returns an optional — always valid.
	TokenKind peek() const;

	// Returns the TokenKind of the token at +lookahead positions.
	TokenKind peek(size_t lookahead) const;

	// Returns the full TokenInfo of the current token.
	// Use when you need the spelling, line, or column.
	const TokenInfo& peek_info() const;

	// Like peek(lookahead) but returns full info.
	TokenInfo peek_info(size_t lookahead) const;

	// Consumes the current token and returns its TokenInfo.
	TokenInfo advance();

	// Consumes the current token only if it matches `kind`.
	// Returns true if consumed.
	bool consume(TokenKind kind);

	// Consumes the current token if it matches; otherwise
	// emits a diagnostic and returns a default TokenInfo.
	TokenInfo expect(TokenKind kind);
};
```

### Before / After Examples

| # | Pattern | Before | After |
|---|---------|--------|-------|
| 1 | Check next token value | `peek_token().has_value() && peek_token()->value() == "{"` | `peek() == "{"_tok` |
| 2 | Check type only | `peek_token().has_value() && peek_token()->type() == Token::Type::Keyword` | `peek().is_keyword()` |
| 3 | Check type + value | `peek_token().has_value() && peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "return"` | `peek() == "return"_tok` |
| 4 | Consume punctuator | `consume_punctuator("{")` | `consume("{"_tok)` |
| 5 | Consume keyword | `consume_keyword("return")` | `consume("return"_tok)` |
| 6 | Get token for diagnostics | `auto tok = consume_token(); use tok->line()` | `auto info = advance(); use info.line()` |
| 7 | EOF loop | `while (peek_token().has_value())` | `while (!peek().is_eof())` |
| 8 | Operator precedence | `peek_token().has_value() && peek_token()->type() == Token::Type::Operator && get_operator_precedence(peek_token()->value()) >= prec` | `peek().is_operator() && get_operator_precedence(peek_info().spelling()) >= prec` |
| 9 | Alternative spellings | `value == "||"` (misses `or`) | `"||"_tok` (equals `"or"_tok` automatically) |
| 10 | Lookahead | `auto next = peek_token(1); if (next.has_value() && next->value() == "[")` | `if (peek(1) == "["_tok)` |

## Lexer Changes

The Lexer's `next_token()` will return `TokenInfo` instead of `Token`.
During lexing, after recognizing the string spelling, the Lexer calls a
`spell_to_kind()` lookup (a `constexpr` hash table or switch) to compute
the `TokenKind`.  This runs once per token, at lex time — all subsequent
comparisons are integer-only.

The Lexer must also map alternative keyword spellings to the canonical
`TokenKind`:

```
"or"      → tok::PipePipe
"and"     → tok::AmpAmp
"not"     → tok::Exclaim
"bitand"  → tok::Amp
"bitor"   → tok::Pipe
"xor"     → tok::Caret
"compl"   → tok::Tilde
"not_eq"  → tok::NotEq
"and_eq"  → tok::AmpEq
"or_eq"   → tok::PipeEq
"xor_eq"  → tok::CaretEq
```

This means the keyword list in `is_keyword()` needs to be extended to
include these alternative tokens, or they can be handled in the operator
consumer as a special case (since `or` etc. are keywords syntactically
but operators semantically).

## TokenKind Constant Namespace

All named constants live in a `tok` namespace:

```cpp
namespace tok {
	// Punctuators
	inline constexpr TokenKind LBrace    { TokenKind::Category::Punctuator,  1 };
	inline constexpr TokenKind RBrace    { TokenKind::Category::Punctuator,  2 };
	inline constexpr TokenKind LParen    { TokenKind::Category::Punctuator,  3 };
	inline constexpr TokenKind RParen    { TokenKind::Category::Punctuator,  4 };
	inline constexpr TokenKind LBracket  { TokenKind::Category::Punctuator,  5 };
	inline constexpr TokenKind RBracket  { TokenKind::Category::Punctuator,  6 };
	inline constexpr TokenKind Semi      { TokenKind::Category::Punctuator,  7 };
	inline constexpr TokenKind Comma     { TokenKind::Category::Punctuator,  8 };
	inline constexpr TokenKind Colon     { TokenKind::Category::Punctuator,  9 };
	inline constexpr TokenKind ColonColon{ TokenKind::Category::Punctuator, 10 };
	inline constexpr TokenKind Ellipsis  { TokenKind::Category::Punctuator, 11 };
	inline constexpr TokenKind Dot       { TokenKind::Category::Punctuator, 12 };
	inline constexpr TokenKind Arrow     { TokenKind::Category::Punctuator, 13 };
	inline constexpr TokenKind Hash      { TokenKind::Category::Punctuator, 14 };

	// Operators
	inline constexpr TokenKind Plus      { TokenKind::Category::Operator,  1 };
	inline constexpr TokenKind Minus     { TokenKind::Category::Operator,  2 };
	inline constexpr TokenKind Star      { TokenKind::Category::Operator,  3 };
	inline constexpr TokenKind Slash     { TokenKind::Category::Operator,  4 };
	inline constexpr TokenKind Percent   { TokenKind::Category::Operator,  5 };
	inline constexpr TokenKind Assign    { TokenKind::Category::Operator,  6 };
	inline constexpr TokenKind EqEq      { TokenKind::Category::Operator,  7 };
	inline constexpr TokenKind NotEq     { TokenKind::Category::Operator,  8 };
	inline constexpr TokenKind Less      { TokenKind::Category::Operator,  9 };
	inline constexpr TokenKind Greater   { TokenKind::Category::Operator, 10 };
	inline constexpr TokenKind LessEq    { TokenKind::Category::Operator, 11 };
	inline constexpr TokenKind GreaterEq { TokenKind::Category::Operator, 12 };
	inline constexpr TokenKind Spaceship { TokenKind::Category::Operator, 13 };
	inline constexpr TokenKind AmpAmp    { TokenKind::Category::Operator, 14 }; // && / and
	inline constexpr TokenKind PipePipe  { TokenKind::Category::Operator, 15 }; // || / or
	inline constexpr TokenKind Exclaim   { TokenKind::Category::Operator, 16 }; // !  / not
	inline constexpr TokenKind Amp       { TokenKind::Category::Operator, 17 }; // &  / bitand
	inline constexpr TokenKind Pipe      { TokenKind::Category::Operator, 18 }; // |  / bitor
	inline constexpr TokenKind Caret     { TokenKind::Category::Operator, 19 }; // ^  / xor
	inline constexpr TokenKind Tilde     { TokenKind::Category::Operator, 20 }; // ~  / compl
	inline constexpr TokenKind PlusEq    { TokenKind::Category::Operator, 21 };
	inline constexpr TokenKind MinusEq   { TokenKind::Category::Operator, 22 };
	inline constexpr TokenKind StarEq    { TokenKind::Category::Operator, 23 };
	inline constexpr TokenKind SlashEq   { TokenKind::Category::Operator, 24 };
	inline constexpr TokenKind PercentEq { TokenKind::Category::Operator, 25 };
	inline constexpr TokenKind AmpEq     { TokenKind::Category::Operator, 26 }; // &= / and_eq
	inline constexpr TokenKind PipeEq    { TokenKind::Category::Operator, 27 }; // |= / or_eq
	inline constexpr TokenKind CaretEq   { TokenKind::Category::Operator, 28 }; // ^= / xor_eq
	inline constexpr TokenKind LessLess  { TokenKind::Category::Operator, 29 };
	inline constexpr TokenKind GreaterGreater { TokenKind::Category::Operator, 30 };
	inline constexpr TokenKind LessLessEq    { TokenKind::Category::Operator, 31 };
	inline constexpr TokenKind GreaterGreaterEq { TokenKind::Category::Operator, 32 };
	inline constexpr TokenKind PlusPlus  { TokenKind::Category::Operator, 33 };
	inline constexpr TokenKind MinusMinus{ TokenKind::Category::Operator, 34 };
	inline constexpr TokenKind Question  { TokenKind::Category::Operator, 35 };
	inline constexpr TokenKind DotStar   { TokenKind::Category::Operator, 36 };
	inline constexpr TokenKind ArrowStar { TokenKind::Category::Operator, 37 };

	// Keywords (assigned incrementally)
	inline constexpr TokenKind KW_if       { TokenKind::Category::Keyword,  1 };
	inline constexpr TokenKind KW_else     { TokenKind::Category::Keyword,  2 };
	inline constexpr TokenKind KW_while    { TokenKind::Category::Keyword,  3 };
	inline constexpr TokenKind KW_for      { TokenKind::Category::Keyword,  4 };
	inline constexpr TokenKind KW_do       { TokenKind::Category::Keyword,  5 };
	inline constexpr TokenKind KW_return   { TokenKind::Category::Keyword,  6 };
	inline constexpr TokenKind KW_class    { TokenKind::Category::Keyword,  7 };
	inline constexpr TokenKind KW_struct   { TokenKind::Category::Keyword,  8 };
	inline constexpr TokenKind KW_enum     { TokenKind::Category::Keyword,  9 };
	inline constexpr TokenKind KW_union    { TokenKind::Category::Keyword, 10 };
	inline constexpr TokenKind KW_namespace{ TokenKind::Category::Keyword, 11 };
	inline constexpr TokenKind KW_template { TokenKind::Category::Keyword, 12 };
	inline constexpr TokenKind KW_typename { TokenKind::Category::Keyword, 13 };
	// ... remaining keywords
}
```

## Migration Strategy

### Phase 0 — Add new types alongside old ones (non-breaking)

1. Add `TokenKind.h` with `TokenKind`, `tok::` constants, `operator""_tok`.
2. Add `TokenInfo.h` wrapping `TokenKind` + location.
3. Add a `TokenKind kind_` field to the **existing** `Token` class.
4. Make the Lexer populate `kind_` during lexing.
5. Add `Token::kind()` accessor.
6. Add `peek()`, `peek_info()`, `advance()`, `consume(TokenKind)` to
   Parser **alongside** the existing methods.

At this point both APIs work and all existing code still compiles.

### Phase 1 — Migrate Parser files one at a time

Pick one Parser file (start with `Parser_Statements.cpp` — smallest at
~10 occurrences), replace all `peek_token().has_value() && peek_token()->value() == "X"`
with `peek() == "X"_tok`, and verify tests pass.

Suggested order (by occurrence count, easiest first):

| Order | File | ~Occurrences |
|-------|------|-------------|
| 1 | Parser_Core.cpp | 2 |
| 2 | Parser_Statements.cpp | 10 |
| 3 | Parser_Types.cpp | 29 |
| 4 | Parser_Expressions.cpp | 105 |
| 5 | Parser_Declarations.cpp | 180 |
| 6 | Parser_Templates.cpp | 186 |

### Phase 2 — Remove old API

Once every call site is migrated:

1. Remove `peek_token()`, `consume_token()`, `consume_keyword()`,
   `consume_punctuator()` from Parser.
2. Remove the old `Token` class.
3. Rename `TokenInfo` → `Token` if desired (or keep the name).

### Phase 3 — Leverage new capabilities

* Add `"or"`, `"and"`, `"not"`, etc. to the Lexer's keyword list so they
  produce `tok::PipePipe`, `tok::AmpAmp`, `tok::Exclaim` automatically.
  This gives FlashCpp correct handling of C++ alternative tokens with
  zero parser changes.
* Use `peek().is_keyword()` / `peek().is_operator()` for
  category-level branches, eliminating the `Token::Type` enum checks.

## Edge Cases

### Identifier Comparison

Identifiers don't have a fixed token string so they all share
`TokenKind::ident()`.  To compare identifier names, use
`peek_info().spelling()`:

```cpp
// Before
if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier &&
    peek_token()->value() == "__pragma") {

// After
if (peek() == TokenKind::ident() && peek_info().spelling() == "__pragma") {
```

This is intentional — identifiers *should* require an explicit spelling
check because there are unboundedly many of them.

### The `>>` Split for Templates

`split_right_shift_token()` currently replaces `current_token_` and
sets `injected_token_`.  The same mechanism works with `TokenInfo`:
two `tok::Greater` tokens are synthesized with adjusted column
numbers.

### Literals

Numeric, string, and character literals each have a category but no
fixed identity.  Like identifiers, comparing their value requires
`peek_info().spelling()`.  The category check is simplified:

```cpp
// Before
peek_token()->type() == Token::Type::StringLiteral

// After
peek().category() == TokenKind::Category::StringLiteral
// or: peek_info().kind().is_string_literal()  (add a helper)
```

## Size and Performance

| Type | Size | Notes |
|------|------|-------|
| `TokenKind` | 4 bytes | category (1 byte) + id (2 bytes) + padding |
| `TokenInfo` | 40 bytes | Same as current `Token` (+ 4 bytes for kind, but could be reduced by dropping the old `type_` field) |
| `Token` (current) | 40 bytes | type (4) + padding (4) + string_view (16) + line (8) + column (8) + file_index (8) — due to alignment |

The hot-path comparison (`peek() == "{"_tok`) compiles to a single 32-bit
compare instruction.  The current path (`peek_token()->value() == "{"`)
performs a `string_view` comparison (pointer + length + memcmp).

## Open Questions

1. **Naming**: `peek()` vs `peek_kind()`? `advance()` vs `next()`?
   The shorter names are proposed here since they'll appear thousands
   of times, but the existing naming convention should take precedence.

2. **Keyword completeness**: The `tok::KW_*` list needs to cover every
   keyword currently in `is_keyword()`.  The MSVC extensions
   (`__int8`, `__declspec`, `__cdecl`, etc.) should get `tok::KW_`
   constants too.

3. **Error recovery**: Should `expect()` return a "poison" `TokenInfo`
   on failure (like Clang does) to reduce error-path branching?

4. **Identifier sub-kinds**: Some identifiers like `override`, `final`
   are context-sensitive keywords.  Should they get their own
   `tok::KW_*` identity, or remain identifiers?  Currently they are
   listed in `is_keyword()`, so giving them a `KW_` constant seems
   consistent.
