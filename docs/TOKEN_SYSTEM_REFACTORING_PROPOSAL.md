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

The literal is driven by a single `constexpr` table — the same table
that the Lexer uses at runtime.  This means there is exactly **one
place** to add a new token; both `_tok` and the Lexer pick it up
automatically.

```cpp
// TokenTable.h — single source of truth for all fixed tokens

struct TokenSpelling {
	std::string_view spelling;
	TokenKind        kind;
};

inline constexpr TokenSpelling all_fixed_tokens[] = {
	// ---- Punctuators ----
	{ "{",   tok::LBrace },    { "}",   tok::RBrace },
	{ "(",   tok::LParen },    { ")",   tok::RParen },
	{ "[",   tok::LBracket },  { "]",   tok::RBracket },
	{ ";",   tok::Semi },      { ",",   tok::Comma },
	{ ":",   tok::Colon },     { "::",  tok::ColonColon },
	{ "...", tok::Ellipsis },  { ".",   tok::Dot },
	{ "->",  tok::Arrow },     { "#",   tok::Hash },

	// ---- Operators ----
	{ "+",   tok::Plus },      { "-",   tok::Minus },
	{ "*",   tok::Star },      { "/",   tok::Slash },
	{ "%",   tok::Percent },   { "=",   tok::Assign },
	{ "==",  tok::EqEq },      { "!=",  tok::NotEq },
	{ "<",   tok::Less },      { ">",   tok::Greater },
	{ "<=",  tok::LessEq },    { ">=",  tok::GreaterEq },
	{ "<=>", tok::Spaceship },
	{ "&&",  tok::AmpAmp },    { "||",  tok::PipePipe },
	{ "!",   tok::Exclaim },   { "&",   tok::Amp },
	{ "|",   tok::Pipe },      { "^",   tok::Caret },
	{ "~",   tok::Tilde },
	{ "+=",  tok::PlusEq },    { "-=",  tok::MinusEq },
	{ "*=",  tok::StarEq },    { "/=",  tok::SlashEq },
	{ "%=",  tok::PercentEq },
	{ "&=",  tok::AmpEq },     { "|=",  tok::PipeEq },
	{ "^=",  tok::CaretEq },
	{ "<<",  tok::LessLess },  { ">>",  tok::GreaterGreater },
	{ "<<=", tok::LessLessEq },{ ">>=", tok::GreaterGreaterEq },
	{ "++",  tok::PlusPlus },  { "--",  tok::MinusMinus },
	{ "?",   tok::Question },  { ".*",  tok::DotStar },
	{ "->*", tok::ArrowStar },

	// ---- Alternative operator spellings (same TokenKind) ----
	{ "and",    tok::AmpAmp },   { "or",     tok::PipePipe },
	{ "not",    tok::Exclaim },  { "bitand", tok::Amp },
	{ "bitor",  tok::Pipe },     { "xor",    tok::Caret },
	{ "compl",  tok::Tilde },    { "not_eq", tok::NotEq },
	{ "and_eq", tok::AmpEq },    { "or_eq",  tok::PipeEq },
	{ "xor_eq", tok::CaretEq },

	// ---- Keywords ----
	{ "if",        tok::KW_if },       { "else",      tok::KW_else },
	{ "while",     tok::KW_while },    { "for",       tok::KW_for },
	{ "do",        tok::KW_do },       { "return",    tok::KW_return },
	{ "class",     tok::KW_class },    { "struct",    tok::KW_struct },
	{ "enum",      tok::KW_enum },     { "union",     tok::KW_union },
	{ "namespace", tok::KW_namespace },{ "template",  tok::KW_template },
	{ "typename",  tok::KW_typename },
	// ... remaining keywords
};
```

The `_tok` literal does a `consteval` search over this table.  Because
it is `consteval`, the entire search is resolved at compile time — there
is zero runtime cost, and an unrecognized string is a compile error.

```cpp
// TokenKind.h

consteval TokenKind operator""_tok(const char* s, size_t len) {
	std::string_view sv(s, len);
	for (const auto& entry : all_fixed_tokens) {
		if (entry.spelling == sv) return entry.kind;
	}
	throw "unrecognized token literal";  // compile error
}
```

For the **Lexer's runtime path**, the same `all_fixed_tokens` table is
used to build a perfect hash at startup (or to generate one offline via
gperf).  A simple approach: use FNV-1a hashing with an open-addressing
table sized to eliminate collisions for the known token set.

```cpp
// Lexer startup: build runtime lookup from the same table
class SpellingToKindMap {
	// Compact open-addressing hash map, sized for zero collisions
	// over the known token set (verified by static_assert).
	static constexpr size_t TABLE_SIZE = /* next power of 2 > 2*N */;
	struct Slot { uint32_t hash; TokenKind kind; };
	std::array<Slot, TABLE_SIZE> slots_{};

public:
	constexpr SpellingToKindMap() {
		for (const auto& entry : all_fixed_tokens) {
			uint32_t h = fnv1a(entry.spelling);
			size_t idx = h & (TABLE_SIZE - 1);
			while (slots_[idx].hash != 0) idx = (idx + 1) & (TABLE_SIZE - 1);
			slots_[idx] = { h, entry.kind };
		}
	}

	TokenKind lookup(std::string_view spelling) const {
		uint32_t h = fnv1a(spelling);
		size_t idx = h & (TABLE_SIZE - 1);
		while (slots_[idx].hash != 0) {
			if (slots_[idx].hash == h) return slots_[idx].kind;
			idx = (idx + 1) & (TABLE_SIZE - 1);
		}
		return TokenKind::eof(); // not a fixed token
	}
};

// The map is constexpr — built at compile time, zero startup cost
inline constexpr SpellingToKindMap spelling_to_kind{};
```

Key properties:

* **Single source of truth** — `all_fixed_tokens` defines every
  spelling-to-kind mapping once.  Both `_tok` and the Lexer derive
  from it.
* `consteval` — `_tok` resolves at compile time; unrecognized strings
  are compile errors.
* `"||"_tok == "or"_tok` — alternative spellings map to the same
  `TokenKind`.
* **Runtime O(1)** — the Lexer uses a perfect hash built from the same
  table.  The hash map is `constexpr`, so it is embedded in the binary
  with no startup cost.

### TokenInfo — full token data (replaces `Token`)

The current `Token` stores a `std::string_view` for the token text.
Downstream code (parser, codegen, AST nodes) frequently converts that
view into a `StringHandle` via `StringTable::getOrInternStringHandle()` —
there are **~123 such call sites** today.  By interning at lex time and
storing a `StringHandle` directly in `TokenInfo`, we:

* **Eliminate ~123 redundant intern calls** — the handle is ready to use.
* **Shrink `TokenInfo`** — `StringHandle` is 4 bytes vs `string_view`'s
  16 bytes (pointer + length).  Total struct drops from 40 → 28 bytes
  (after alignment: 32 bytes).
* **Make handle equality the default** — comparing two tokens' text is a
  single `uint32_t` compare instead of `memcmp`.
* **Guarantee identity** — two tokens with the same text always share
  the same `StringHandle`, which is useful for symbol table lookups
  without rehashing.

The original `string_view` is still recoverable via
`StringHandle::view()` for diagnostics or string operations.

```cpp
// TokenInfo.h
#include "TokenKind.h"
#include "StringTable.h"

class TokenInfo {
public:
	TokenInfo() = default;
	TokenInfo(TokenKind kind, StringHandle spelling,
	          size_t line, size_t column, size_t file_index)
		: kind_(kind), spelling_(spelling),
		  line_(line), column_(column), file_index_(file_index) {}

	TokenKind        kind()       const { return kind_; }
	StringHandle     handle()     const { return spelling_; }
	std::string_view text()      const { return spelling_.view(); }
	size_t           line()       const { return line_; }
	size_t           column()     const { return column_; }
	size_t           file_index() const { return file_index_; }

	// Convenience: compare directly with TokenKind
	bool operator==(TokenKind k) const { return kind_ == k; }
	bool operator!=(TokenKind k) const { return kind_ != k; }

private:
	TokenKind    kind_;            // 4 bytes
	StringHandle spelling_;        // 4 bytes
	size_t       line_   = 0;      // 8 bytes
	size_t       column_ = 0;      // 8 bytes
	size_t       file_index_ = 0;  // 8 bytes
	// Total: 32 bytes (with alignment)
};
```

`TokenInfo` is the full replacement for the old `Token` class.  It
carries location info and the interned spelling handle, but can be
compared cheaply via its `TokenKind`.

The `handle()` accessor returns a `StringHandle` that can be passed
directly to symbol tables, AST nodes, and codegen without re-interning.
The `text()` convenience accessor returns a `string_view` for when you
need the actual characters (error messages, debug output, etc.).

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
| 8 | Operator precedence | `peek_token().has_value() && peek_token()->type() == Token::Type::Operator && get_operator_precedence(peek_token()->value()) >= prec` | `peek().is_operator() && get_operator_precedence(peek_info().text()) >= prec` |
| 9 | Alternative spellings | `value == "||"` (misses `or`) | `"||"_tok` (equals `"or"_tok` automatically) |
| 10 | Lookahead | `auto next = peek_token(1); if (next.has_value() && next->value() == "[")` | `if (peek(1) == "["_tok)` |
| 11 | Intern for symbol table | `StringHandle h = StringTable::getOrInternStringHandle(token.value())` | `StringHandle h = info.handle()` (already interned) |

## Lexer Changes

The Lexer's `next_token()` will return `TokenInfo` instead of `Token`.
During lexing, after recognizing the string spelling, the Lexer:

1. Calls `spell_to_kind()` (a `constexpr` hash table or switch) to
   compute the `TokenKind`.  This runs once per token, at lex time —
   all subsequent comparisons are integer-only.

2. Interns the spelling via `StringTable::getOrInternStringHandle()` to
   produce a `StringHandle`.  For fixed tokens (keywords, operators,
   punctuators) the handles are stable and could be pre-interned at
   startup for zero-cost lookup.  For identifiers and literals, the
   interning happens once here rather than being scattered across 123
   call sites in the parser and codegen.

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

IDs within each category are assigned by per-category enums, so adding
a new token is just appending to the enum — no manual numbering, no
risk of duplicates or gaps.

```cpp
// TokenKind.h

// ---- Per-category ID enums (auto-incremented) ----

enum class PunctId : uint16_t {
	LBrace, RBrace, LParen, RParen, LBracket, RBracket,
	Semi, Comma, Colon, ColonColon, Ellipsis, Dot, Arrow, Hash
};

enum class OpId : uint16_t {
	Plus, Minus, Star, Slash, Percent, Assign,
	EqEq, NotEq, Less, Greater, LessEq, GreaterEq, Spaceship,
	AmpAmp,     // && / and
	PipePipe,   // || / or
	Exclaim,    // !  / not
	Amp,        // &  / bitand
	Pipe,       // |  / bitor
	Caret,      // ^  / xor
	Tilde,      // ~  / compl
	PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
	AmpEq,      // &= / and_eq
	PipeEq,     // |= / or_eq
	CaretEq,    // ^= / xor_eq
	LessLess, GreaterGreater, LessLessEq, GreaterGreaterEq,
	PlusPlus, MinusMinus, Question, DotStar, ArrowStar
};

enum class KeywordId : uint16_t {
	If, Else, While, For, Do, Return,
	Class, Struct, Enum, Union, Namespace,
	Template, Typename, Typedef, Using,
	Const, Static, Virtual, Override, Final,
	Public, Private, Protected, Friend,
	Void, Int, Auto,
	Switch, Case, Default, Break, Continue,
	New, Delete, Try, Catch, Throw,
	Sizeof, Constexpr,
	StaticCast, DynamicCast, ConstCast, ReinterpretCast,
	// ... remaining keywords appended here
};
```

`TokenKind` gets factory methods that pair the category with the enum
value, so the two can never go out of sync:

```cpp
class TokenKind {
	// ... (existing members) ...

	// Typed factories — category is implicit from the enum type
	static constexpr TokenKind punct(PunctId id) {
		return { Category::Punctuator, static_cast<uint16_t>(id) };
	}
	static constexpr TokenKind op(OpId id) {
		return { Category::Operator, static_cast<uint16_t>(id) };
	}
	static constexpr TokenKind kw(KeywordId id) {
		return { Category::Keyword, static_cast<uint16_t>(id) };
	}
};
```

The `tok::` constants are one-liners with no manual indices:

```cpp
namespace tok {
	// Punctuators
	inline constexpr auto LBrace     = TokenKind::punct(PunctId::LBrace);
	inline constexpr auto RBrace     = TokenKind::punct(PunctId::RBrace);
	inline constexpr auto LParen     = TokenKind::punct(PunctId::LParen);
	inline constexpr auto RParen     = TokenKind::punct(PunctId::RParen);
	inline constexpr auto LBracket   = TokenKind::punct(PunctId::LBracket);
	inline constexpr auto RBracket   = TokenKind::punct(PunctId::RBracket);
	inline constexpr auto Semi       = TokenKind::punct(PunctId::Semi);
	inline constexpr auto Comma      = TokenKind::punct(PunctId::Comma);
	inline constexpr auto Colon      = TokenKind::punct(PunctId::Colon);
	inline constexpr auto ColonColon = TokenKind::punct(PunctId::ColonColon);
	inline constexpr auto Ellipsis   = TokenKind::punct(PunctId::Ellipsis);
	inline constexpr auto Dot        = TokenKind::punct(PunctId::Dot);
	inline constexpr auto Arrow      = TokenKind::punct(PunctId::Arrow);
	inline constexpr auto Hash       = TokenKind::punct(PunctId::Hash);

	// Operators
	inline constexpr auto Plus       = TokenKind::op(OpId::Plus);
	inline constexpr auto Minus      = TokenKind::op(OpId::Minus);
	inline constexpr auto Star       = TokenKind::op(OpId::Star);
	inline constexpr auto Slash      = TokenKind::op(OpId::Slash);
	inline constexpr auto Percent    = TokenKind::op(OpId::Percent);
	inline constexpr auto Assign     = TokenKind::op(OpId::Assign);
	inline constexpr auto EqEq       = TokenKind::op(OpId::EqEq);
	inline constexpr auto NotEq      = TokenKind::op(OpId::NotEq);
	inline constexpr auto Less       = TokenKind::op(OpId::Less);
	inline constexpr auto Greater    = TokenKind::op(OpId::Greater);
	inline constexpr auto LessEq     = TokenKind::op(OpId::LessEq);
	inline constexpr auto GreaterEq  = TokenKind::op(OpId::GreaterEq);
	inline constexpr auto Spaceship  = TokenKind::op(OpId::Spaceship);
	inline constexpr auto AmpAmp     = TokenKind::op(OpId::AmpAmp);
	inline constexpr auto PipePipe   = TokenKind::op(OpId::PipePipe);
	inline constexpr auto Exclaim    = TokenKind::op(OpId::Exclaim);
	inline constexpr auto Amp        = TokenKind::op(OpId::Amp);
	inline constexpr auto Pipe       = TokenKind::op(OpId::Pipe);
	inline constexpr auto Caret      = TokenKind::op(OpId::Caret);
	inline constexpr auto Tilde      = TokenKind::op(OpId::Tilde);
	// ... remaining operators follow the same pattern

	// Keywords
	inline constexpr auto KW_if        = TokenKind::kw(KeywordId::If);
	inline constexpr auto KW_else      = TokenKind::kw(KeywordId::Else);
	inline constexpr auto KW_while     = TokenKind::kw(KeywordId::While);
	inline constexpr auto KW_for       = TokenKind::kw(KeywordId::For);
	inline constexpr auto KW_return    = TokenKind::kw(KeywordId::Return);
	inline constexpr auto KW_class     = TokenKind::kw(KeywordId::Class);
	inline constexpr auto KW_struct    = TokenKind::kw(KeywordId::Struct);
	inline constexpr auto KW_namespace = TokenKind::kw(KeywordId::Namespace);
	inline constexpr auto KW_template  = TokenKind::kw(KeywordId::Template);
	inline constexpr auto KW_typename  = TokenKind::kw(KeywordId::Typename);
	// ... remaining keywords follow the same pattern
}
```

Adding a new token requires two steps:
1. Append to the relevant enum (`PunctId`, `OpId`, or `KeywordId`)
2. Add a `tok::` constant and a row in `all_fixed_tokens`

No manual index management.  The compiler enforces uniqueness through
the enum, and the `all_fixed_tokens` table ties spellings to constants.

## Migration Strategy

### Phase 0 — Add new types alongside old ones (non-breaking)

1. Add `TokenKind.h` with `TokenKind`, `tok::` constants, `operator""_tok`.
2. Add `TokenInfo.h` wrapping `TokenKind` + `StringHandle` + location.
3. Add `TokenKind kind_` and `StringHandle handle_` fields to
   the **existing** `Token` class.
4. Make the Lexer populate both `kind_` and `handle_` during
   lexing (intern via `StringTable::getOrInternStringHandle()`).
5. Add `Token::kind()` and `Token::handle()` accessors.
6. Add `peek()`, `peek_info()`, `advance()`, `consume(TokenKind)` to
   Parser **alongside** the existing methods.

At this point both APIs work and all existing code still compiles.
Call sites can start using `token.handle()` instead of
`StringTable::getOrInternStringHandle(token.value())` immediately.

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
`peek_info().handle()` (returns a `StringHandle`) or
`peek_info().text()` (returns a `string_view`):

```cpp
// Before
if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier &&
    peek_token()->value() == "__pragma") {

// After — using StringHandle comparison (uint32_t compare, fastest)
if (peek() == TokenKind::ident() && peek_info().handle() == pragma_handle) {

// After — using text() for ad-hoc string comparison
if (peek() == TokenKind::ident() && peek_info().text() == "__pragma") {
```

Note: `StringHandle` already has `operator==(std::string_view)`, so
`peek_info().handle() == "__pragma"` works directly.  But for
frequently compared identifiers, caching the `StringHandle` is
preferred since it reduces the comparison to a single integer check.

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
`peek_info().text()` (for the string content) or `peek_info().handle()`
(for the interned handle).  The category check is simplified:

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
| `StringHandle` | 4 bytes | packed uint32_t (chunk index + offset) |
| `TokenInfo` | 32 bytes | kind (4) + spelling (4) + line (8) + column (8) + file_index (8) |
| `Token` (current) | 40 bytes | type (4) + padding (4) + string_view (16) + line (8) + column (8) + file_index (8) |

Switching from `string_view` (16 bytes) to `StringHandle` (4 bytes)
shrinks `TokenInfo` by 8 bytes compared to the current `Token` (32 vs
40 bytes).  This also means `std::optional<TokenInfo>` (during the
transition) is smaller than the current `std::optional<Token>`.

The hot-path comparison (`peek() == "{"_tok`) compiles to a single 32-bit
compare instruction.  The current path (`peek_token()->value() == "{"`)
performs a `string_view` comparison (pointer + length + memcmp).

Interning at lex time adds a one-time cost per token (hash + lookup),
but this is offset by eliminating ~123 `getOrInternStringHandle()` calls
that currently happen later in the pipeline.  For fixed tokens
(keywords, operators, punctuators) the Lexer can pre-intern a static
table at startup, making the per-token cost a simple table lookup.

## Open Questions

1. **Naming**: `peek()` vs `peek_kind()`? `advance()` vs `next()`?
   The shorter names are proposed here since they'll appear thousands
   of times, but the existing naming convention should take precedence.
   `handle()` is chosen for the `StringHandle` accessor — short and
   unambiguous given the return type.

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

5. **Pre-interned fixed tokens**: Should the Lexer maintain a static
   table of pre-interned `StringHandle`s for all keywords, operators,
   and punctuators?  This would make the per-token intern cost for
   fixed tokens a simple array lookup instead of a hash table probe.

6. **AST node storage**: AST nodes currently store full `Token` objects
   (e.g. `identifier_token()` returns `const Token&`).  With
   `StringHandle` in `TokenInfo`, those AST nodes could store just
   the `StringHandle` + location where they don't need the
   `TokenKind`, or the full `TokenInfo` where they do.  This is a
   follow-up optimization worth considering after the core migration.
