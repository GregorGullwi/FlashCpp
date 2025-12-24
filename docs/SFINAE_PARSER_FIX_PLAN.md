# SFINAE Parser Fix Plan

## Problem Statement

The FlashCpp parser currently fails to compile SFINAE (Substitution Failure Is Not An Error) patterns with nested template expressions like:

```cpp
template<typename T>
typename enable_if<is_int<T>::value, int>::type
process_int_type(T val) {
    return val + 100;
}
```

The specific issue is parsing return types of the form `typename enable_if<is_int<T>::value, int>::type` where:
1. The template argument contains a nested template with member access: `is_int<T>::value`
2. The result type itself requires member access: `::type`

## Root Cause Analysis

### Current Behavior

When parsing `typename enable_if<is_int<T>::value, int>::type`, the parser encounters a fundamental architectural issue:

1. **Expression Parsing in Template Arguments**: The first template argument `is_int<T>::value` is a dependent expression that includes:
   - Template instantiation: `is_int<T>`
   - Member access: `::value`

2. **Eager Qualified Identifier Consumption**: When `parse_type_specifier()` is called recursively to parse inner types (like `is_int<T>` as part of evaluating the expression), it eagerly checks for and consumes `::member` patterns.

3. **Token Misattribution**: The `::type` suffix that belongs to the outer `enable_if<...>` template gets incorrectly consumed by inner `parse_type_specifier()` calls parsing `is_int<T>`, causing:
   - The inner parser to create placeholders for `is_int<...>::type` (wrong!)
   - The outer template argument parser to see unexpected tokens like the function name
   - Template argument parsing to fail and backtrack

### Call Stack Example

```
parse_type_and_name() [for function declaration]
└─> parse_type_specifier() [for return type: enable_if<...>::type]
    ├─> parse_explicit_template_arguments() [for enable_if<...>]
    │   ├─> parse_expression(2) [first arg: is_int<T>::value] ✓ SUCCESS
    │   │   └─> Parses correctly, accepts as dependent expression
    │   │
    │   └─> parse_type_specifier() [second arg: int]
    │       └─> After parsing 'int', sees '>' and returns ✓
    │
    └─> Check for '::' after template args
        └─> Finds '::type' and parses it ✓ SHOULD WORK HERE

BUT IN BACKTRACKING ATTEMPTS:
parse_explicit_template_arguments()
└─> parse_expression(2) fails to parse as literal
    └─> Falls through to parse_type_specifier()
        └─> Parses is_int<T>
            └─> After template args, checks for '::'
                └─> INCORRECTLY consumes '::type' ✗ WRONG LEVEL!
```

## Architectural Solutions

### Solution 1: Context-Aware Parsing (Recommended)

**Approach**: Add a parsing context flag to track when we're inside template argument parsing.

**Implementation**:
```cpp
class Parser {
    // Add member variable
    int template_arg_parsing_depth_ = 0;
    
    // In parse_explicit_template_arguments:
    template_arg_parsing_depth_++;
    // ... parse arguments ...
    template_arg_parsing_depth_--;
    
    // In parse_type_specifier, when checking for '::':
    if (peek_token()->value() == "::") {
        // Only parse qualified identifiers if NOT inside template arg parsing
        // OR if this is an explicitly qualified type like std::vector
        if (template_arg_parsing_depth_ == 0 || has_explicit_namespace) {
            // Parse ::member
        }
    }
};
```

**Pros**:
- Minimal invasive changes
- Clear semantic meaning
- Easy to debug with depth tracking
- Prevents incorrect token consumption at wrong levels

**Cons**:
- Adds parser state that needs careful management
- Requires testing edge cases with deeply nested templates

**Files to Modify**:
- `src/Parser.h`: Add `template_arg_parsing_depth_` member
- `src/Parser.cpp`: 
  - Increment/decrement in `parse_explicit_template_arguments()`
  - Add checks in `parse_type_specifier()` before parsing `::`

### Solution 2: Deferred Qualified Name Resolution

**Approach**: Don't parse `::member` during type parsing; let higher-level code handle it.

**Implementation**:
```cpp
// parse_type_specifier returns base type only
// New function: parse_qualified_type_specifier
ParseResult Parser::parse_qualified_type_specifier() {
    auto base_result = parse_type_specifier(); // Gets 'enable_if<...>'
    if (base_result.is_error()) return base_result;
    
    // Now check for qualified members at THIS level
    while (peek_token()->value() == "::") {
        // Parse ::member
    }
    
    return result;
}

// Call sites change from:
auto type = parse_type_specifier();
// To:
auto type = parse_qualified_type_specifier();
```

**Pros**:
- Clear separation of concerns
- Base type parsing vs. qualified name resolution
- No parser state needed

**Cons**:
- More invasive - requires changing many call sites
- Risk of breaking existing code that expects full qualified types
- Need to audit all `parse_type_specifier()` call sites

**Files to Modify**:
- `src/Parser.h`: Add `parse_qualified_type_specifier()` declaration
- `src/Parser.cpp`:
  - Create `parse_qualified_type_specifier()`
  - Modify `parse_type_specifier()` to only parse base types
  - Update call sites in:
    - `parse_type_and_name()`
    - `parse_declaration()`
    - Other top-level type parsing contexts

### Solution 3: Two-Phase Type Resolution

**Approach**: Parse types in two phases - syntactic then semantic resolution.

**Implementation**:
```cpp
// Phase 1: Parse syntactic structure, create AST nodes
//          Don't try to resolve or instantiate anything
ParseResult parse_type_syntax();

// Phase 2: Resolve types, handle dependent members
TypeSpecifierNode resolve_type(const TypeSyntaxNode& syntax);

// For dependent types:
if (has_dependent_args) {
    // Store the full qualified name as a string
    // "enable_if<is_int<T>::value, int>::type"
    // Resolve during instantiation
}
```

**Pros**:
- Clean separation: syntax vs. semantics
- Better for complex template scenarios
- Aligns with C++ standard's two-phase lookup

**Cons**:
- Major architectural change
- Requires significant refactoring
- Would need new AST node types for unresolved syntax

**Files to Modify**:
- Many - this is a large refactoring

## Recommended Implementation Plan

### Phase 1: Context-Aware Parsing (Immediate Fix)

1. **Add context tracking** (1-2 hours)
   - Add `template_arg_parsing_depth_` to `Parser` class
   - Increment/decrement in `parse_explicit_template_arguments()`

2. **Modify qualified identifier parsing** (2-3 hours)
   - In `parse_type_specifier()`, check depth before parsing `::`
   - Add special handling for explicitly qualified types (e.g., `std::vector`)

3. **Test thoroughly** (2-3 hours)
   - Test `test_sfinae_overload_resolution.cpp`
   - Test `test_sfinae_enable_if.cpp`
   - Test `test_sfinae_is_same.cpp`
   - Verify no regressions in existing template tests

### Phase 2: Comprehensive Testing (Follow-up)

4. **Edge cases** (1-2 hours)
   - Deeply nested templates
   - Multiple dependent arguments
   - Mixed dependent and non-dependent arguments

5. **Documentation** (1 hour)
   - Document the context tracking mechanism
   - Add comments explaining the depth check logic

## Alternative Considerations

### Special Token Lookahead

Instead of tracking depth, use sophisticated lookahead to determine if `::` belongs to current type or outer context:

```cpp
// Before consuming '::', check if this makes sense
// For example, if next tokens are '::type >' then this might be
// a member of a template instantiation
if (peek_token()->value() == "::" && 
    peek_ahead(2)->value() != ">" && 
    peek_ahead(2)->value() != ",") {
    // Likely a qualified member, parse it
}
```

**Issue**: Fragile, doesn't handle all cases, hard to maintain.

### Expression vs. Type Disambiguation

Make `parse_explicit_template_arguments()` smarter about when to try type parsing:

```cpp
// Only try type parsing if expression parsing truly failed
// Not just "can't evaluate yet"
if (!expr_result.is_error() && expr_result.node().has_value()) {
    // We got an expression - don't try type parsing
    // Even if we can't evaluate it yet
}
```

**Issue**: Already partially implemented, but still has the `::` consumption problem.

## Testing Strategy

### Unit Tests

1. Simple SFINAE patterns
2. Nested template expressions in template arguments
3. Multiple levels of nesting
4. Mixed dependent and non-dependent arguments

### Integration Tests

1. `test_sfinae_overload_resolution.cpp` - primary failing test
2. `test_sfinae_enable_if.cpp` - basic SFINAE
3. `test_sfinae_is_same.cpp` - type traits
4. Existing template tests for regressions

### Manual Testing

```bash
# Build compiler
make main CXX=clang++

# Test specific file
cd /tmp
/path/to/FlashCpp test_sfinae_overload_resolution.cpp -o test.o

# Link and run
clang test.o -o test
./test
echo $?  # Should be 0 for success
```

## Success Criteria

1. ✅ `test_sfinae_overload_resolution.cpp` compiles successfully
2. ✅ All related SFINAE tests pass
3. ✅ No regressions in existing template tests
4. ✅ Code is well-documented and maintainable
5. ✅ Performance impact is negligible (depth tracking is O(1))

## Timeline Estimate

- **Solution 1 (Context-Aware)**: 6-10 hours
- **Solution 2 (Deferred Resolution)**: 15-20 hours
- **Solution 3 (Two-Phase)**: 40+ hours

**Recommendation**: Implement Solution 1 (Context-Aware Parsing) as it provides the best balance of effectiveness, maintainability, and implementation effort.
