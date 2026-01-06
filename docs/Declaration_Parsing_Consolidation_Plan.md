# Declaration Parsing Consolidation Plan

## Implementation Progress

| Phase | Status | Commit |
|-------|--------|--------|
| Phase 1: Extract Shared Specifier Parsing | ✅ Complete | e1b6a07 |
| Phase 2: Add Function Detection to parse_variable_declaration | ✅ Complete | e34f3c2 |
| Phase 3: Consolidate Initialization Handling | ✅ Complete | (this PR) |
| Phase 4: Full Unification (Optional) | ⏳ Pending | - |

## Executive Summary

This document outlines a plan to consolidate the two parallel declaration parsing paths in FlashCpp's parser: `parse_declaration_or_function_definition()` and `parse_variable_declaration()`. The current dual-path architecture leads to code duplication, maintenance burden, and subtle parsing inconsistencies.

## Current Architecture

### Two Parallel Parsing Functions

#### 1. `parse_declaration_or_function_definition()` (Lines ~1941-2670)
**Purpose**: Designed for top-level (global/namespace scope) declarations.

**Handles**:
- Function declarations and definitions
- Global variable declarations
- Member function out-of-line definitions (`Class::method()`)
- Template function declarations
- Attributes (`[[nodiscard]]`, `__declspec`, calling conventions)
- Storage class specifiers (`static`, `extern`)
- Constexpr/constinit/consteval specifiers
- Comma-separated global variable declarations

**Limitations**:
- Does NOT handle direct initialization syntax: `int x(10);`
- Does NOT handle structured bindings
- Does NOT handle type deduction guides for `auto`

#### 2. `parse_variable_declaration()` (Lines ~9803-10200)
**Purpose**: Designed for local (block scope) variable declarations.

**Handles**:
- Local variable declarations
- Direct initialization: `int x(10);`
- Brace initialization: `int x{10};`
- Copy initialization: `int x = 10;`
- Structured bindings: `auto [a, b] = pair;`
- Storage class specifiers (`static`, `extern`, `register`, `mutable`)
- Constexpr/constinit specifiers
- Comma-separated declarations with BlockNode wrapping
- Type deduction and deduction guides

**Limitations**:
- Does NOT handle function declarations
- When it sees `int func()`, it misparses `()` as direct initialization

### The Dispatch Problem

The `parse_statement_or_declaration()` function uses a keyword dispatch table:

```cpp
// Current dispatch (after revert):
{"int", &Parser::parse_variable_declaration},    // Local vars
{"void", &Parser::parse_declaration_or_function_definition},  // Functions
{"static", &Parser::parse_variable_declaration}, // Problem: static functions!
```

**Issues**:
1. `static int func() {}` in a block is routed to `parse_variable_declaration`, which can't handle functions
2. `int main() {}` inside a block is misparsed as a variable with direct initialization
3. Code duplication for specifier parsing (~50 lines duplicated)

## Proposed Consolidation

### Option A: Single Unified Parser (Recommended)

Create one unified function that handles all declaration forms:

```cpp
ParseResult Parser::parse_declaration(DeclarationContext context) {
    // context: TopLevel, BlockScope, ClassMember, ForInit, etc.
    
    // 1. Parse specifiers (shared code)
    DeclarationSpecifiers specs = parse_declaration_specifiers();
    
    // 2. Parse declarator (type + name)
    ParseResult declarator = parse_declarator();
    
    // 3. Determine what we have based on next token
    if (peek_is("(") && looks_like_function_params()) {
        return parse_function_declaration(specs, declarator, context);
    } else if (peek_is("(")) {
        return parse_direct_initialization(specs, declarator);
    } else if (peek_is("{")) {
        return parse_brace_initialization(specs, declarator);
    } else if (peek_is("=")) {
        return parse_copy_initialization(specs, declarator);
    } else if (peek_is(";")) {
        return parse_uninitialized_declaration(specs, declarator);
    }
    // ... etc
}
```

### Option B: Helper Function Approach

Keep both functions but extract shared code:

```cpp
// Shared specifier parsing
struct DeclarationSpecifiers {
    StorageClass storage_class;
    bool is_constexpr;
    bool is_constinit;
    bool is_consteval;
    bool is_inline;
    bool is_static;
    bool is_extern;
    AttributeInfo attributes;
    CallingConvention calling_convention;
};

DeclarationSpecifiers Parser::parse_declaration_specifiers() {
    // Single implementation used by both functions
}

// parse_variable_declaration becomes a helper called by parse_declaration_or_function_definition
ParseResult Parser::parse_variable_declaration_impl(
    const DeclarationSpecifiers& specs,
    DeclarationNode& decl,
    TypeSpecifierNode& type_spec
) {
    // Handle initialization forms
    // Handle comma-separated declarations
    // Return VariableDeclarationNode
}
```

### Option C: Rename and Clarify Roles

If keeping separate functions, rename for clarity:

```cpp
// For top-level only
ParseResult Parser::parse_toplevel_declaration();

// For block scope, but can detect and delegate function declarations  
ParseResult Parser::parse_local_declaration_or_statement();
```

## Recommended Implementation Plan

### Phase 1: Extract Shared Specifier Parsing (Low Risk) ✅ COMPLETE

**Implementation Details:**
- Created `DeclarationSpecifiers` struct in `ParserTypes.h`
- Created `parse_declaration_specifiers()` helper in `Parser.cpp`
- Updated `parse_declaration_or_function_definition()` to use helper
- Updated `parse_variable_declaration()` to use helper
- Eliminated ~45 lines of duplicated code
- All 832 tests pass

1. Create `DeclarationSpecifiers` struct
2. Create `parse_declaration_specifiers()` helper
3. Update both functions to use the helper
4. **Test thoroughly** - this is a refactoring with no behavior change

### Phase 2: Add Function Detection to parse_variable_declaration (Medium Risk) ✅ COMPLETE

**Implementation Details:**
- Created `looks_like_function_parameters()` helper that uses lookahead to distinguish:
  - `int x()` - empty = function (prefer function)
  - `int x(int y)` - starts with type = function params  
  - `int x(10)` - literal = direct init
  - `int x(a)` where `a` is a type = function params
  - `int x(a)` where `a` is a variable = direct init
- Updated `parse_variable_declaration()` to check for function params before direct init
- If function detected, delegates to `parse_function_declaration()` with full body parsing
- All 832 tests pass

1. After parsing type and name, check if next token is `(`
2. Use lookahead to distinguish function params from direct init:
   - `int x(10)` - literal/expression = direct init
   - `int x(int y)` - type = function params
   - `int x()` - empty = could be either, prefer function
3. If function detected, delegate to function parsing logic
4. **Test with edge cases**: `int x(int)`, `int x(())`, `int x(a)` where `a` is a type

### Phase 3: Consolidate Initialization Handling (Medium Risk) ✅ COMPLETE

**Implementation Details:**
- Created `parse_direct_initialization()` helper for `Type var(args)` form
- Created `parse_copy_initialization()` helper for `Type var = expr` and `Type var = {args}` forms
- The existing `parse_brace_initializer()` handles direct brace init `Type var{args}`
- Updated `parse_variable_declaration()` to use the shared helpers
- Updated `parse_declaration_or_function_definition()` to use `parse_copy_initialization()` for copy and copy-list init forms
- Updated comma-separated declaration handling to use `parse_copy_initialization()`
- Auto type deduction and array size inference handled within `parse_copy_initialization()`
- All 832 tests pass

1. Extract initialization parsing to shared helpers:
   - `parse_direct_initialization()` - parses `Type var(args)`
   - `parse_brace_initialization()` - already existed as `parse_brace_initializer()`
   - `parse_copy_initialization()` - parses `Type var = expr` and `Type var = {args}`
2. Both main functions call these helpers
3. **Test all initialization forms**

### Phase 4: Full Unification (High Risk, Optional)

1. Create unified `parse_declaration()` with context parameter
2. Gradually migrate callers
3. Deprecate old functions
4. **Extensive regression testing required**

## Code Duplication to Eliminate

### Currently Duplicated (~100 lines total):

1. **Specifier parsing loops** (~30 lines each)
   - `constexpr`, `constinit`, `consteval` detection
   - `static`, `extern`, `register`, `mutable` detection
   - Order handling (`static constexpr` vs `constexpr static`)

2. **Type and name parsing call** (~5 lines each)
   - `parse_type_and_name()` invocation
   - Error handling

3. **Initialization detection** (~20 lines each)
   - Checking for `=`, `{`, `(`
   - Dispatching to appropriate handler

4. **Symbol table insertion** (~10 lines each)
   - `gSymbolTable.insert()` call
   - Duplicate symbol error handling

## Testing Strategy

### Unit Tests to Add:

```cpp
// Function declarations in blocks
TEST_CASE("Block scope function declaration") {
    // int helper();  // forward declaration in block
    // static int helper() { return 1; }  // static function in block
}

// Direct init vs function ambiguity
TEST_CASE("Most vexing parse cases") {
    // int x(int());  // function declaration, not variable
    // int x((int())); // variable with direct init
    // int x(int(y)); // depends on whether y is a type
}

// All initialization forms
TEST_CASE("Initialization forms") {
    // int x;          // default
    // int x = 1;      // copy
    // int x(1);       // direct
    // int x{1};       // brace
    // int x = {1};    // copy-list
}
```

### Regression Tests:
- Run full test suite after each phase
- Pay special attention to:
  - Lambda tests (complex initialization)
  - Template tests (deduction)
  - Struct tests (member functions)

## Timeline Estimate

| Phase | Effort | Risk | Dependencies |
|-------|--------|------|--------------|
| Phase 1 | 2-3 hours | Low | None |
| Phase 2 | 4-6 hours | Medium | Phase 1 |
| Phase 3 | 3-4 hours | Medium | Phase 1 |
| Phase 4 | 8-12 hours | High | Phases 1-3 |

**Recommended**: Complete Phases 1-3 for significant improvement with manageable risk. Phase 4 is optional and should only be done if the codebase needs major refactoring anyway.

## Appendix: Current Function Signatures

```cpp
// Current signatures
ParseResult Parser::parse_declaration_or_function_definition();
ParseResult Parser::parse_variable_declaration();

// Proposed new signatures (Phase 4)
ParseResult Parser::parse_declaration(DeclarationContext context = DeclarationContext::Auto);

enum class DeclarationContext {
    Auto,           // Infer from current scope
    TopLevel,       // Global/namespace scope
    BlockScope,     // Inside function body
    ClassMember,    // Inside class/struct
    ForInit,        // for(HERE; ...; ...)
    IfInit,         // if(HERE; condition)
    SwitchInit,     // switch(HERE; condition)
    LambdaCapture,  // [HERE]() {}
};
```

## References

- Original audit: `docs/C++20_Grammar_Audit.md`
- C++20 Standard: Section 9 (Declarations)
- Most Vexing Parse: https://en.wikipedia.org/wiki/Most_vexing_parse
