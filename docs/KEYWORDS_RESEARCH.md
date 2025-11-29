# FlashCpp Lexer Keywords Research

## Research Question

Can the Lexer return a combined type and value pair for keywords? Instead of:
```cpp
if (has_value() && type() == Keyword && value() == "constexpr")
```

Can this be replaced with:
```cpp
if (type() == Keyword_constexpr)
```

This document explores the benefits, drawbacks, and implementation strategies for this approach, along with related optimizations.

---

## Current Implementation Analysis

### Token Structure (Current)

```cpp
class Token {
public:
    enum class Type {
        Uninitialized,
        Identifier,
        Keyword,          // All 100 keywords share one type
        Literal,
        StringLiteral,
        CharacterLiteral,
        Operator,
        Punctuator,
        EndOfFile
    };
    
private:
    Type type_;
    std::string_view value_;   // Stores the actual keyword text
    size_t line_, column_, file_index_;
};
```

### Current Keyword Checking Pattern

In `Parser.cpp`, keyword checks require two comparisons:

```cpp
// Pattern 1: Direct check (103 occurrences)
if (peek_token()->type() == Token::Type::Keyword && 
    peek_token()->value() == "constexpr")

// Pattern 2: Via consume_keyword helper (19 usages)
if (consume_keyword("for"sv))

// Pattern 3: With has_value() guard (383 has_value() calls total)
if (peek_token().has_value() && 
    peek_token()->type() == Token::Type::Keyword &&
    peek_token()->value() == "template")
```

### Current Statistics

| Metric | Count |
|--------|-------|
| Total keywords in Lexer | 100 |
| Keyword type checks in Parser.cpp | 103 |
| `value()` string comparisons in Parser.cpp | 402 |
| `has_value()` checks in Parser.cpp | 383 |
| `consume_keyword()` usages | 19 |
| Lines in Parser.cpp | 18,617 |

### Most Frequently Checked Keywords

| Keyword | Count | Context |
|---------|-------|---------|
| `default` | 9 | switch/default, default/delete specifiers |
| `class` | 9 | class declarations, template class |
| `struct` | 7 | struct declarations |
| `delete` | 7 | delete keyword, deleted functions |
| `virtual` | 5 | virtual functions, virtual bases |
| `enum` | 5 | enum declarations |
| `template` | 4 | template declarations |
| `requires` | 4 | C++20 requires clauses |
| `using` | 4 | using declarations/directives |
| `namespace` | 4 | namespace declarations |

---

## Approach 1: Combined Type/Value Enum (Keyword_constexpr)

### Design

```cpp
enum class TokenType : uint16_t {
    // Non-keyword types
    Uninitialized = 0,
    Identifier,
    Literal,
    StringLiteral,
    CharacterLiteral,
    Operator,
    Punctuator,
    EndOfFile,
    
    // Keyword types (starting from a high value for easy range checking)
    KeywordBase = 256,
    
    // Type keywords
    Keyword_void,
    Keyword_bool,
    Keyword_char,
    Keyword_char8_t,
    Keyword_char16_t,
    Keyword_char32_t,
    Keyword_short,
    Keyword_int,
    Keyword_long,
    Keyword_float,
    Keyword_double,
    Keyword_signed,
    Keyword_unsigned,
    Keyword_auto,
    Keyword_decltype,
    Keyword_wchar_t,
    
    // Storage class specifiers
    Keyword_static,
    Keyword_extern,
    Keyword_register,
    Keyword_mutable,
    Keyword_thread_local,
    
    // CV qualifiers
    Keyword_const,
    Keyword_volatile,
    
    // Constexpr specifiers
    Keyword_constexpr,
    Keyword_consteval,
    Keyword_constinit,
    
    // Access specifiers
    Keyword_public,
    Keyword_protected,
    Keyword_private,
    
    // Class/struct keywords
    Keyword_class,
    Keyword_struct,
    Keyword_union,
    Keyword_enum,
    
    // Control flow
    Keyword_if,
    Keyword_else,
    Keyword_switch,
    Keyword_case,
    Keyword_default,
    Keyword_for,
    Keyword_while,
    Keyword_do,
    Keyword_break,
    Keyword_continue,
    Keyword_return,
    Keyword_goto,
    
    // Exception handling
    Keyword_try,
    Keyword_catch,
    Keyword_throw,
    Keyword_noexcept,
    
    // Other keywords
    Keyword_namespace,
    Keyword_using,
    Keyword_typedef,
    Keyword_template,
    Keyword_typename,
    Keyword_concept,
    Keyword_requires,
    Keyword_this,
    Keyword_nullptr,
    Keyword_true,
    Keyword_false,
    Keyword_new,
    Keyword_delete,
    Keyword_sizeof,
    Keyword_alignas,
    Keyword_alignof,
    Keyword_static_assert,
    Keyword_friend,
    Keyword_virtual,
    Keyword_override,
    Keyword_final,
    Keyword_explicit,
    Keyword_inline,
    Keyword_operator,
    Keyword_typeid,
    Keyword_export,
    Keyword_asm,
    
    // Cast keywords
    Keyword_static_cast,
    Keyword_dynamic_cast,
    Keyword_const_cast,
    Keyword_reinterpret_cast,
    
    // Alternative operators (ISO 646)
    Keyword_and,
    Keyword_or,
    Keyword_xor,
    Keyword_not,
    Keyword_bitand,
    Keyword_bitor,
    Keyword_compl,
    Keyword_and_eq,
    Keyword_or_eq,
    Keyword_xor_eq,
    Keyword_not_eq,
    
    // Microsoft-specific
    Keyword___int8,
    Keyword___int16,
    Keyword___int32,
    Keyword___int64,
    Keyword___ptr32,
    Keyword___ptr64,
    Keyword___w64,
    Keyword___unaligned,
    Keyword___uptr,
    Keyword___sptr,
    
    KeywordEnd  // Sentinel for range checking
};
```

### Implementation in Lexer

```cpp
Token consume_identifier_or_keyword() {
    // ... tokenize identifier ...
    std::string_view value = source_.substr(start, cursor_ - start);
    
    // Lookup in keyword map instead of just checking membership
    auto it = keyword_type_map_.find(value);
    if (it != keyword_type_map_.end()) {
        return Token(it->second, value, line_, column_, current_file_index_);
    }
    return Token(TokenType::Identifier, value, line_, column_, current_file_index_);
}

// Keyword map (static, initialized once)
static const std::unordered_map<std::string_view, TokenType> keyword_type_map_ = {
    {"void", TokenType::Keyword_void},
    {"int", TokenType::Keyword_int},
    {"constexpr", TokenType::Keyword_constexpr},
    // ... all 100 keywords ...
};
```

### Parser Usage

```cpp
// Before
if (peek_token()->type() == Token::Type::Keyword && 
    peek_token()->value() == "constexpr")

// After
if (peek_token()->type() == TokenType::Keyword_constexpr)
```

### Benefits

1. **Single Comparison**: Reduces keyword checks from 2-3 comparisons to 1
2. **Type Safety**: Impossible to misspell keywords - caught at compile time
3. **Better IDE Support**: Autocomplete works for enum values
4. **Range Checking**: Easy to check if token is any keyword:
   ```cpp
   bool isKeyword() const {
       return type_ >= TokenType::KeywordBase && type_ < TokenType::KeywordEnd;
   }
   ```
5. **Switch Statements**: Can use switch instead of if-else chains:
   ```cpp
   switch (token.type()) {
       case TokenType::Keyword_if:
       case TokenType::Keyword_while:
       case TokenType::Keyword_for:
           // Handle loop constructs
           break;
   }
   ```
6. **Performance**: Integer comparison is faster than string comparison
7. **Memory**: No need to store `value_` for keywords (could be computed)

### Drawbacks

1. **Larger Enum**: Type enum grows from 9 to ~110 entries
2. **Lexer Complexity**: Must use hash map instead of set for keyword lookup
3. **Migration Effort**: All 103+ keyword checks must be updated
4. **Error Messages**: Need helper to get string from keyword type for diagnostics
5. **Value Still Needed**: For error messages, `value()` must still work

### Performance Analysis

Current (string comparison):
- `type()` comparison: 1 integer compare
- `value()` comparison: ~7-10 character comparisons average
- Total: ~8-11 operations per keyword check

Proposed (single enum):
- `type()` comparison: 1 integer compare
- Total: 1 operation per keyword check

Estimated improvement: 8-10x faster for keyword checks

Impact on overall parsing: Keyword checks are not the bottleneck, but many are in hot paths (type parsing, declaration parsing). Estimated 5-15% improvement in lexer/early-parser performance.

---

## Approach 2: Keyword Subtypes by Parsing Context

### Motivation

Different keywords have different semantic meanings in different contexts. Grouping them can simplify parser logic.

### Proposed Keyword Categories

```cpp
enum class KeywordCategory : uint8_t {
    TypeKeyword,        // int, float, void, etc.
    StorageClass,       // static, extern, register, mutable, thread_local
    CVQualifier,        // const, volatile
    AccessSpecifier,    // public, private, protected
    ControlFlow,        // if, else, for, while, switch, etc.
    ClassDefinition,    // class, struct, union, enum
    Operator,           // operator, sizeof, new, delete, typeid
    Template,           // template, typename, concept, requires
    Exception,          // try, catch, throw, noexcept
    Declaration,        // typedef, using, namespace
    FunctionSpecifier,  // inline, virtual, explicit, override, final
    ConstexprSpec,      // constexpr, consteval, constinit
    CastKeyword,        // static_cast, dynamic_cast, etc.
    Literal,            // true, false, nullptr, this
    AlternativeOp,      // and, or, xor, not, etc.
    Other               // alignas, alignof, asm, export, etc.
};
```

### Token Extension

```cpp
class Token {
public:
    TokenType type() const;
    KeywordCategory keyword_category() const;  // Only valid if isKeyword()
    
    bool isTypeKeyword() const {
        return keyword_category() == KeywordCategory::TypeKeyword;
    }
    bool isStorageClass() const {
        return keyword_category() == KeywordCategory::StorageClass;
    }
    // etc.
};
```

### Parser Usage

```cpp
// Before: Check each type keyword individually
if (peek_token()->value() == "int" || 
    peek_token()->value() == "float" ||
    peek_token()->value() == "double" || ...)

// After: Check category
if (peek_token()->isTypeKeyword())
```

### Benefits

1. **Semantic Grouping**: Easier to reason about valid keywords in each context
2. **Simplified Parser Logic**: Category checks replace multiple keyword checks
3. **Documentation**: Categories document keyword semantics
4. **Error Messages**: "Expected type keyword" instead of listing all valid keywords

### Drawbacks

1. **Additional Field**: Token needs to store category (1 byte)
2. **Maintenance**: Must keep categories in sync with keywords
3. **Not Always Clear**: Some keywords fit multiple categories (e.g., `const`)

---

## Approach 3: Context-Specific Lexer Modes

### Concept

The Lexer could operate in different modes depending on parsing context, returning different token types or doing different processing.

### Possible Modes

```cpp
enum class LexerMode {
    Normal,           // Standard C++ lexing
    TemplateArgs,     // Inside <...> - treat >> as two > tokens
    Preprocessor,     // After # - different token rules
    RawString,        // Inside R"(...)" - minimal processing
    Attributes,       // Inside [[...]] - attribute-specific parsing
};
```

### Template Argument Mode Example

One of the most compelling use cases:

```cpp
// Problem: >> is lexed as right-shift in normal mode
std::vector<std::vector<int>> // >> must be two > tokens

// Solution: Lexer mode for template arguments
void Parser::parse_template_arguments() {
    lexer_.setMode(LexerMode::TemplateArgs);
    // >> is now returned as two separate > tokens
    // ... parse ...
    lexer_.setMode(LexerMode::Normal);
}
```

### Benefits

1. **Correct >> Handling**: No post-processing needed for `>>` in templates
2. **Better Error Recovery**: Mode-specific error handling
3. **Preprocessor Integration**: Clean separation of preprocessor tokens
4. **Raw String Handling**: Simplified raw string literal parsing

### Drawbacks

1. **State Management**: Parser must carefully manage lexer state
2. **Complexity**: More modes = more complex lexer
3. **Bugs**: Mode mismatches can cause subtle parsing errors
4. **Performance**: Mode checks add overhead to every token
5. **Testing**: Each mode needs separate test coverage

### Recommendation

Selective adoption - only implement modes where there's clear benefit:

- Template argument mode for `>>`: High value, localized impact
- Raw string mode: Already somewhat necessary
- Avoid general keyword modes: Too much complexity for little benefit

---

## Alternative: Helper Functions (Minimal Change)

Instead of restructuring the Token type, add helper functions to simplify checking:

```cpp
// In Token class
bool isKeyword(std::string_view kw) const {
    return type_ == Type::Keyword && value_ == kw;
}

// Usage
if (peek_token()->isKeyword("constexpr"))

// Category helpers
bool isTypeKeyword() const {
    static const std::unordered_set<std::string_view> type_kws = {
        "int", "float", "double", "char", "bool", "void", ...
    };
    return type_ == Type::Keyword && type_kws.count(value_);
}
```

### Benefits
- Minimal code change
- Easy to implement incrementally
- No Token size increase
- Backward compatible

### Drawbacks
- Still does string comparison
- Category functions have runtime overhead
- Not compile-time checkable

---

## Recommendations

### Short-Term (Low Effort, High Value)

1. **Add `Token::isKeyword(string_view)` helper**
   - Reduces verbosity
   - No structural changes
   - Easy to migrate gradually

2. **Add keyword category helpers**
   - `isTypeKeyword()`, `isStorageClass()`, etc.
   - Static lookup tables
   - Simplifies parser checks

### Medium-Term (Moderate Effort)

3. **Implement combined type enum**
   - Replace `Token::Type::Keyword` with specific `Keyword_xxx` types
   - Keep `value()` for error messages
   - Update parser incrementally

### Long-Term (Consider Carefully)

4. **Template argument lexer mode**
   - Only for `>>` handling
   - Localized scope management

5. **Consider NOT implementing**:
   - General lexer modes for keywords
   - Complex context-dependent keyword handling
   - The complexity cost outweighs benefits

---

## Implementation Priority

| Change | Effort | Value | Priority |
|--------|--------|-------|----------|
| `isKeyword(str)` helper | Low | Medium | 1 |
| Keyword category helpers | Low | Medium | 2 |
| Combined type enum | High | High | 3 |
| Template `>>` mode | Medium | Medium | 4 |

---

## Conclusion

The combined type/value enum approach (Approach 1) offers the best balance of performance improvement and code clarity. It eliminates string comparisons, provides compile-time safety, and enables cleaner switch statements.

However, the migration effort is significant. A phased approach is recommended:

1. Start with helper functions (immediate cleanup)
2. Implement the combined enum infrastructure
3. Migrate keyword checks incrementally (can be done over time)
4. Consider lexer modes only for specific pain points (>> in templates)

The keyword category system (Approach 2) can be implemented alongside the combined enum with minimal overhead - each keyword type can be assigned to a category via a simple lookup table.

Context-specific lexer modes (Approach 3) should be used sparingly and only where they solve specific problems that can't be handled cleanly in the parser.
