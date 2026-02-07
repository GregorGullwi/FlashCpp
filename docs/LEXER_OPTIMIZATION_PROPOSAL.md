# Lexer Optimization Proposal

Companion to [TOKEN_SYSTEM_REFACTORING_PROPOSAL.md](TOKEN_SYSTEM_REFACTORING_PROPOSAL.md).
These optimizations target the Lexer's hot paths and can be implemented
independently of the token system refactoring, though they compose well
with it (e.g. the `SpellingToKindMap` replaces `is_keyword()`).

## Lexer Dispatch Table Optimization

### Problem

The current `next_token()` loop classifies each character through a
chain of `if`/`else if` branches:

```
isspace → consume_whitespace
'#'     → consume_file_info
'/'     → comment or operator
'L'     → wide literal check
isalpha → identifier/keyword
isdigit → literal
'-'     → negative literal check
'.'     → float literal check
'"'     → string literal
'\''    → character literal
is_operator  → consume_operator    (unordered_set lookup!)
is_punctuator → consume_punctuator (unordered_set lookup!)
else    → skip
```

There are three performance issues:

1. **Branch mispredictions** — The CPU sees a cascade of unpredictable
   conditional jumps.  In typical C++ source, identifiers and
   whitespace dominate, but operators and punctuators are common enough
   to keep the branch predictor guessing.

2. **`unordered_set` lookups for single chars** — `is_operator()` and
   `is_punctuator()` each hash a single `char` into an
   `unordered_set`.  This is a heap-chasing hash lookup for what should
   be a table[256] bit test.

3. **`isalpha`/`isdigit`/`isspace` calls** — These are locale-aware
   function calls.  In a compiler that only handles ASCII source, they
   can be replaced with direct table lookups.

### Solution: 256-entry dispatch table

Replace the entire if-else chain with a single array index:

```cpp
// Lexer.h — character classification + dispatch

// What action to take for each byte value
enum class CharAction : uint8_t {
	Skip,           // unknown/control character — advance and retry
	Whitespace,     // ' ', '\t', '\r', '\n', etc.
	Alpha,          // a-z A-Z _
	Digit,          // 0-9
	Slash,          // / — could be comment or operator
	Quote,          // "
	SingleQuote,    // '
	Hash,           // #
	Dot,            // . — could be float literal or punctuator
	Minus,          // - — could be negative literal or operator
	LetterL,        // L — could be wide literal or identifier
	Operator,       // + * % ^ & | ~ ! = < > ?
	Punctuator,     // ( ) [ ] { } , ; :
	Eof,            // '\0' — end of input sentinel
};

// Built at compile time — one entry per byte value
inline constexpr auto char_table = [] {
	std::array<CharAction, 256> t{};
	// Default: Skip
	for (auto& a : t) a = CharAction::Skip;

	// Whitespace
	t[' ']  = CharAction::Whitespace;
	t['\t'] = CharAction::Whitespace;
	t['\n'] = CharAction::Whitespace;
	t['\r'] = CharAction::Whitespace;
	t['\f'] = CharAction::Whitespace;
	t['\v'] = CharAction::Whitespace;

	// Alpha + underscore
	for (int c = 'a'; c <= 'z'; ++c) t[c] = CharAction::Alpha;
	for (int c = 'A'; c <= 'Z'; ++c) t[c] = CharAction::Alpha;
	t['_'] = CharAction::Alpha;
	// Override 'L' for wide literal detection
	t['L'] = CharAction::LetterL;

	// Digits
	for (int c = '0'; c <= '9'; ++c) t[c] = CharAction::Digit;

	// Special single characters
	t['/']  = CharAction::Slash;
	t['"']  = CharAction::Quote;
	t['\''] = CharAction::SingleQuote;
	t['#']  = CharAction::Hash;
	t['.']  = CharAction::Dot;
	t['-']  = CharAction::Minus;

	// Operators (excluding / - . which have special handling)
	for (char c : {'+','*','%','^','&','|','~','!','=','<','>','?'})
		t[(unsigned char)c] = CharAction::Operator;

	// Punctuators
	for (char c : {'(',')','{','}','[',']',',',';',':'})
		t[(unsigned char)c] = CharAction::Punctuator;

	t['\0'] = CharAction::Eof;
	return t;
}();
```

The hot loop becomes a `switch` on the table value — which the compiler
lowers to a jump table (one indirect jump, no branch prediction needed):

```cpp
Token next_token() {
	while (cursor_ < source_size_) {
		const char c = source_[cursor_];
		switch (char_table[static_cast<unsigned char>(c)]) {

		case CharAction::Whitespace:
			consume_whitespace();
			continue;

		case CharAction::Alpha:
			return consume_identifier_or_keyword();

		case CharAction::LetterL:
			if (cursor_ + 1 < source_size_) {
				char next = source_[cursor_ + 1];
				if (next == '"')  { ++cursor_; ++column_; return consume_prefixed_string_literal(cursor_ - 1); }
				if (next == '\'') { ++cursor_; ++column_; return consume_prefixed_character_literal(cursor_ - 1); }
			}
			return consume_identifier_or_keyword();

		case CharAction::Digit:
			return consume_literal();

		case CharAction::Slash:
			if (cursor_ + 1 < source_size_) {
				char next = source_[cursor_ + 1];
				if (next == '/') { consume_single_line_comment(); continue; }
				if (next == '*') { consume_multi_line_comment(); continue; }
			}
			return consume_operator();

		case CharAction::Quote:
			return consume_string_literal();

		case CharAction::SingleQuote:
			return consume_character_literal();

		case CharAction::Hash:
			if (cursor_ + 1 < source_size_ && source_[cursor_ + 1] >= '0'
			    && source_[cursor_ + 1] <= '9') {
				consume_file_info();
				continue;
			}
			return consume_punctuator();

		case CharAction::Dot:
			if (cursor_ + 1 < source_size_ && source_[cursor_ + 1] >= '0'
			    && source_[cursor_ + 1] <= '9') {
				return consume_literal();
			}
			return consume_punctuator();

		case CharAction::Minus:
			if (cursor_ + 1 < source_size_ && source_[cursor_ + 1] >= '0'
			    && source_[cursor_ + 1] <= '9') {
				return consume_literal();
			}
			return consume_operator();

		case CharAction::Operator:
			return consume_operator();

		case CharAction::Punctuator:
			return consume_punctuator();

		case CharAction::Eof:
			goto done;

		case CharAction::Skip:
			++cursor_;
			++column_;
			continue;
		}
	}
done:
	return Token(Token::Type::EndOfFile, ""sv, line_, column_,
		current_file_index_);
}
```

### Replacing `is_operator()` and `is_punctuator()`

The current implementations use `static unordered_set<char>` — a heap-
allocated hash table to classify single bytes.  With the dispatch table,
these functions are no longer needed; the main loop handles classification
directly.  For any code that still needs them outside the lexer:

```cpp
// O(1) table lookup, no heap, no hashing
bool is_operator(char c) const {
	auto a = char_table[static_cast<unsigned char>(c)];
	return a == CharAction::Operator || a == CharAction::Slash
	    || a == CharAction::Minus;
}

bool is_punctuator(char c) const {
	auto a = char_table[static_cast<unsigned char>(c)];
	return a == CharAction::Punctuator || a == CharAction::Dot
	    || a == CharAction::Hash;
}
```

### Replacing `is_keyword()` with the `SpellingToKindMap`

The current `is_keyword()` uses a `static unordered_set<string_view>`
with ~80 entries, rebuilt on first call.  With the token system refactoring,
this is replaced by the `SpellingToKindMap` (the constexpr perfect hash
from the `all_fixed_tokens` table).  After consuming an identifier,
the lexer does:

```cpp
Token consume_identifier_or_keyword() {
	size_t start = cursor_;
	// Scan identifier body — use the same table to avoid isalnum()
	while (cursor_ < source_size_) {
		auto a = char_table[static_cast<unsigned char>(source_[cursor_])];
		if (a != CharAction::Alpha && a != CharAction::Digit
		    && a != CharAction::LetterL)
			break;
		++cursor_;
		++column_;
	}

	std::string_view value = source_.substr(start, cursor_ - start);
	TokenKind kind = spelling_to_kind.lookup(value);
	if (!kind.is_eof()) {
		// It's a keyword (or alternative token like "or", "and")
		return Token(/* kind */, value, line_, column_, current_file_index_);
	}
	return Token(/* TokenKind::ident() */, value, line_, column_,
		current_file_index_);
}
```

This eliminates the `unordered_set<string_view>` entirely and replaces
it with the same `constexpr` hash table that `_tok` uses — a single
data structure shared between compile-time and runtime paths.

### Replacing `isalnum()` in hot identifier scanning

The identifier inner loop (`while isalnum || '_'`) calls a locale-aware
function per character.  The dispatch table already classifies these, so
the inner loop becomes a table check (shown above).  Alternatively, a
dedicated 256-bit lookup table (32 bytes) can be used:

```cpp
// 256-bit bitmap: is this byte part of an identifier?
inline constexpr auto ident_table = [] {
	std::array<uint8_t, 32> t{};  // 256 bits
	auto set = [&](unsigned char c) { t[c >> 3] |= (1 << (c & 7)); };
	for (int c = 'a'; c <= 'z'; ++c) set(c);
	for (int c = 'A'; c <= 'Z'; ++c) set(c);
	for (int c = '0'; c <= '9'; ++c) set(c);
	set('_');
	return t;
}();

inline bool is_ident_char(char c) {
	auto u = static_cast<unsigned char>(c);
	return (ident_table[u >> 3] >> (u & 7)) & 1;
}
```

This is a single memory load + shift + mask — no function call, no
branch, no locale.

### Summary of changes

| Component | Before | After |
|-----------|--------|-------|
| Main dispatch | 12-branch if-else chain | `switch` on `char_table[c]` (jump table) |
| `is_operator()` | `unordered_set<char>::count()` | `char_table[c]` comparison |
| `is_punctuator()` | `unordered_set<char>::count()` | `char_table[c]` comparison |
| `is_keyword()` | `unordered_set<string_view>::count()` | `spelling_to_kind.lookup()` (constexpr hash) |
| `isalpha`/`isdigit`/`isspace` | Locale-aware function calls | Table lookups |
| Identifier inner loop | `isalnum(c) \|\| c == '_'` per char | `is_ident_char(c)` — bit test |

All tables are `constexpr` — zero initialization cost, embedded in
`.rodata`.

## SIMD Whitespace and Identifier Scanning

### Problem

Whitespace is the most frequently encountered "token" in C++ source.
Preprocessed headers in particular can have long runs of blank lines,
indentation, and trailing spaces.  The current `consume_whitespace()`
processes one byte at a time:

```cpp
void consume_whitespace() {
    while (cursor_ < source_size_ && std::isspace(source_[cursor_])) {
        if (source_[cursor_] == '\n') {
            ++line_;
            column_ = 1;
            update_file_index_from_line();
        }
        else {
            ++column_;
        }
        ++cursor_;
    }
}
```

Each iteration does: a bounds check, a locale-aware `isspace()` call,
a newline branch, and three increments.  For a 50-character indentation
line, that's 50 iterations with 50 branches.

The same problem applies to identifier scanning — the inner loop calls
`isalnum()` per character, which is also locale-aware and single-byte.

### Approach: scan 16 bytes at a time with SSE2

SSE2 is universally available on x86-64 (it's part of the baseline
ABI).  The idea: load 16 bytes from the source buffer, compare all 16
against whitespace characters simultaneously, and find the position of
the first non-whitespace byte.

```cpp
#include <immintrin.h>

// Skip whitespace in bulk — handles the common case of spaces/tabs
// (no newlines) in 16-byte strides.  Falls back to scalar for newlines.
void consume_whitespace() {
    // Fast path: skip runs of spaces/tabs with SIMD
    while (cursor_ + 16 <= source_size_) {
        __m128i chunk = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(source_.data() + cursor_));

        // Check for space (0x20) and tab (0x09)
        __m128i is_space = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(' '));
        __m128i is_tab   = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\t'));
        __m128i is_ws    = _mm_or_si128(is_space, is_tab);

        int mask = _mm_movemask_epi8(is_ws);
        if (mask == 0xFFFF) {
            // All 16 bytes are spaces or tabs — skip entire chunk
            cursor_ += 16;
            column_ += 16;
            continue;
        }

        // Some bytes are not whitespace — find the first one
        // But first check: could any of them be newlines?
        __m128i is_nl = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\n'));
        int nl_mask = _mm_movemask_epi8(is_nl);

        if (nl_mask == 0) {
            // No newlines in this chunk.
            // Count leading whitespace bits.
            int ws_count = __builtin_ctz(~mask);  // first non-ws byte
            cursor_ += ws_count;
            column_ += ws_count;
            return;  // done — next byte is non-whitespace
        }

        // Newline found in this chunk — fall back to scalar for
        // correct line/column tracking up to and including the
        // newline, then resume SIMD on the next line.
        break;
    }

    // Scalar fallback: handles newlines + tail < 16 bytes
    while (cursor_ < source_size_) {
        char c = source_[cursor_];
        if (c == ' ' || c == '\t') {
            ++cursor_;
            ++column_;
        } else if (c == '\n') {
            ++cursor_;
            ++line_;
            column_ = 1;
            update_file_index_from_line();
        } else if (c == '\r') {
            ++cursor_;
            // Don't increment column for \r
        } else {
            return;  // non-whitespace
        }
    }
}
```

### Why this is effective

In typical C++ source (especially preprocessed output), the most common
whitespace pattern is **indentation at the start of a line**: 4-40
spaces or tabs with no newlines.  The SIMD path handles this in 1-3
iterations instead of 4-40.

The scalar fallback handles newlines correctly — newlines require
`line_` and `column_` updates plus `update_file_index_from_line()`, so
they can't be skipped in bulk.  But newlines are relatively rare
compared to horizontal whitespace, so the SIMD fast path covers the
common case.

### SIMD identifier scanning

The same approach applies to `consume_identifier_or_keyword()`:

```cpp
Token consume_identifier_or_keyword() {
    size_t start = cursor_;
    ++cursor_;
    ++column_;

    // SIMD fast path: scan 16 bytes for identifier characters
    // Identifier chars: [a-z] [A-Z] [0-9] _
    while (cursor_ + 16 <= source_size_) {
        __m128i chunk = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(source_.data() + cursor_));

        // Range check: is each byte in [a-z], [A-Z], [0-9], or '_'?
        // Use saturating subtraction trick for range checks:
        //   byte_in_range(lo, hi) = (b - lo) <= (hi - lo)
        //   which is: _mm_cmpeq_epi8(_mm_max_epu8(sub, limit), limit)

        // [a-z]: subtract 'a', check <= 25
        __m128i lo_az = _mm_subs_epu8(chunk, _mm_set1_epi8('a'));
        __m128i in_az = _mm_cmplt_epu8(lo_az, _mm_set1_epi8(26));

        // [A-Z]: subtract 'A', check <= 25
        __m128i lo_AZ = _mm_subs_epu8(chunk, _mm_set1_epi8('A'));
        __m128i in_AZ = _mm_cmplt_epu8(lo_AZ, _mm_set1_epi8(26));

        // [0-9]: subtract '0', check <= 9
        __m128i lo_09 = _mm_subs_epu8(chunk, _mm_set1_epi8('0'));
        __m128i in_09 = _mm_cmplt_epu8(lo_09, _mm_set1_epi8(10));

        // '_': exact match
        __m128i in_us = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('_'));

        // Combine: any of the above
        __m128i is_ident = _mm_or_si128(
            _mm_or_si128(in_az, in_AZ),
            _mm_or_si128(in_09, in_us));

        int mask = _mm_movemask_epi8(is_ident);
        if (mask == 0xFFFF) {
            // All 16 bytes are identifier chars — skip entire chunk
            cursor_ += 16;
            column_ += 16;
            continue;
        }

        // Find first non-identifier byte
        int ident_count = __builtin_ctz(~mask);
        cursor_ += ident_count;
        column_ += ident_count;
        goto done;
    }

    // Scalar tail
    while (cursor_ < source_size_ && is_ident_char(source_[cursor_])) {
        ++cursor_;
        ++column_;
    }

done:
    std::string_view value = source_.substr(start, cursor_ - start);
    TokenKind kind = spelling_to_kind.lookup(value);
    if (!kind.is_eof()) {
        return Token(/* kind */, value, line_, column_, current_file_index_);
    }
    return Token(/* TokenKind::ident() */, value, line_, column_,
        current_file_index_);
}
```

Note: `_mm_cmplt_epu8` doesn't exist as a single instruction — it's
implemented as `_mm_cmpeq_epi8(_mm_max_epu8(a, limit), limit)` with an
inverted result, or via `_mm_adds_epu8` + compare.  The exact intrinsic
sequence depends on whether readability or minimal instruction count is
preferred; the compiler usually optimizes it well either way.

### Portability

SSE2 is baseline for all x86-64 targets (guaranteed by the AMD64 ABI).
For ARM targets (e.g. Apple Silicon, Linux aarch64), the equivalent
uses NEON intrinsics (`vld1q_u8`, `vceqq_u8`, etc.) — the structure is
identical but the intrinsic names differ.

A portable fallback can use the scalar `is_ident_char()` table from
the dispatch table section — this is still faster than `isalnum()` and
works everywhere.

### Expected impact

| Scenario | Bytes/iteration (before) | Bytes/iteration (after) |
|----------|--------------------------|-------------------------|
| Indentation (spaces only) | 1 | 16 |
| Long identifier (`very_long_variable_name`) | 1 | 16 |
| Short identifier (`x`, `i`) | 1 | 1 (scalar) |
| Newlines | 1 | 1 (scalar fallback) |

For preprocessed source (which can be 50-100x larger than the original
due to header expansion), whitespace often accounts for 30-50% of all
bytes.  A 16x throughput improvement on this portion translates to a
measurable overall speedup.
