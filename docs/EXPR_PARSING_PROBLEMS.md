# Expression Parsing Problems in FlashCpp

## Overview

This document outlines known issues and limitations in FlashCpp's expression parser, specifically related to template argument list disambiguation. These issues were discovered during investigation of test compilation failures.

## The Classic C++ Template Argument Disambiguation Problem

### Problem Description

C++ has a fundamental parsing ambiguity when encountering the `<` token after an identifier in expression context. The parser must decide whether:

1. `<` starts a template argument list (e.g., `function<int, float>(args)`)
2. `<` is a less-than comparison operator (e.g., `a < b`)

This ambiguity becomes particularly problematic when template argument lists contain commas, as the comma can also be interpreted as the comma operator.

### Example

```cpp
template<typename... Args>
int sum(Args... args);

template<>
int sum<int, int>(int arg0, int arg1) {
    return arg0 + arg1;
}

int main() {
    // This fails to parse in FlashCpp:
    int result = sum<int, int>(3, 7);
    
    // Parser interprets as: (sum < int), int > (3, 7)
    // Instead of: sum<int, int>(3, 7)
}
```

## Current Behavior in FlashCpp

### What Works ✅

1. **Single-argument template calls**
   ```cpp
   int a = sum<int>(5);  // Works correctly
   ```

2. **Template argument deduction**
   ```cpp
   int b = sum(3, 7);  // Works - deduces template args from function args
   ```

3. **Function template specialization parsing**
   ```cpp
   template<>
   int sum<int, int>(int arg0, int arg1) { ... }  // Parses correctly
   ```

### What Doesn't Work ❌

1. **Multi-argument template calls**
   ```cpp
   int c = sum<int, int>(3, 7);  // FAILS - parser error
   int d = sum<int, int, int>(1, 2, 4);  // FAILS - parser error
   ```

## Root Cause Analysis

### Parser Flow

1. **Expression parsing** (`parse_expression` in Parser.cpp, line ~7888)
   - Calls `parse_unary_expression()` to get the left operand
   - Then looks for binary operators including `<`

2. **Unary expression parsing** (`parse_unary_expression` in Parser.cpp, line ~7549)
   - Handles identifiers and attempts template argument parsing
   - At line ~9797, checks if identifier is followed by `<`
   - Calls `parse_explicit_template_arguments()` to try parsing template args

3. **Template argument parsing** (`parse_explicit_template_arguments` in Parser.cpp, line ~15167)
   - Successfully parses template arguments including commas
   - Returns `std::optional<std::vector<TemplateTypeArg>>`

4. **The Problem**
   - When `parse_explicit_template_arguments()` succeeds for `<int, int>`, control returns to unary expression parsing
   - The parser checks for function call syntax (line ~9880-9881)
   - However, by this point the expression parser has already started treating `<` as an operator
   - The parsing state becomes inconsistent, leading to failure

### Why Single Arguments Work

With single template arguments like `sum<int>()`:
- No comma is present in the template argument list
- Parser doesn't encounter the comma-operator ambiguity
- Template argument parsing completes successfully
- Function call is recognized and parsed correctly

### Why Multiple Arguments Fail

With multiple template arguments like `sum<int, int>()`:
- Parser sees `sum<int`
- Encounters comma, which could be:
  - Part of template argument list: `sum<int, int>`
  - Comma operator: `(sum < int), int`
- Current implementation has insufficient lookahead to disambiguate
- Parsing fails before reaching the function call parentheses

## Technical Details

### Relevant Code Locations

1. **Expression parsing**: `src/Parser.cpp:7888` - `parse_expression()`
2. **Unary expression parsing**: `src/Parser.cpp:7549` - `parse_unary_expression()`
3. **Template argument parsing**: `src/Parser.cpp:15167` - `parse_explicit_template_arguments()`
4. **Identifier handling with templates**: `src/Parser.cpp:9795-9841`
5. **Function call recognition**: `src/Parser.cpp:9880-9881`

### Current Template Argument Parsing Logic

```cpp
// Line 9797 in parse_unary_expression
if (peek_token().has_value() && peek_token()->value() == "<") {
    explicit_template_args = parse_explicit_template_arguments();
    // If parsing failed, it might be a less-than operator, so continue normally
}

// Line 9880-9881
bool is_function_call = peek_token().has_value() && peek_token()->value() == "(" &&
    (is_function_decl || is_function_pointer || has_operator_call || 
     explicit_template_args.has_value() || is_template_parameter);
```

The logic attempts to parse template arguments and check for function calls, but the surrounding expression parser context interferes with multi-argument cases.

## Solutions and Workarounds

### Workaround for Users

Use template argument deduction instead of explicit template arguments:

```cpp
// Instead of:
int result = sum<int, int>(3, 7);  // FAILS

// Use:
int result = sum(3, 7);  // WORKS - deduces <int, int> from arguments
```

### Potential Solutions for FlashCpp

#### Solution 1: Enhanced Lookahead (Recommended)

Implement sophisticated lookahead when encountering `identifier<`:

1. Save parser state
2. Attempt to parse as template argument list
3. Check if followed by `(` for function call or `::` for qualified name
4. If successful, commit the parse
5. If failed, restore state and parse as less-than operator

**Pros:**
- Handles all template call cases correctly
- Minimal impact on non-template code
- Follows C++ standard disambiguation rules

**Cons:**
- Requires careful state management
- May impact parsing performance
- Needs extensive testing

#### Solution 2: Context-Aware Parsing

Maintain context about whether we're in a position where a template call is expected:

1. Check if identifier is registered as a template function
2. If yes, aggressively try template argument parsing
3. Use the template's parameter count to guide parsing

**Pros:**
- Can leverage existing template registry
- More targeted fix

**Cons:**
- May not handle all edge cases
- Requires symbol table lookups during parsing
- Could fail for templates defined later in file

#### Solution 3: Backtracking Parser

Implement full backtracking support in the parser:

1. Try parsing as template call
2. If fails, backtrack and try as comparison
3. Use longest-match or priority rules

**Pros:**
- Most robust solution
- Handles all ambiguities

**Cons:**
- Major parser architecture change
- Significant performance impact
- Complex implementation

## Recommended Implementation Plan

### Phase 1: Enhanced Lookahead for Template Calls

1. **Modify `parse_unary_expression` around line 9797:**
   ```cpp
   if (peek_token().has_value() && peek_token()->value() == "<") {
       // Save parser state
       auto saved_pos = save_token_position();
       
       // Try parsing template arguments
       auto explicit_template_args = parse_explicit_template_arguments();
       
       if (explicit_template_args.has_value()) {
           // Check if followed by ( or ::
           if (peek_token().has_value() && 
               (peek_token()->value() == "(" || peek_token()->value() == "::")) {
               // This is definitely a template - keep the parse
               discard_saved_token(saved_pos);
               // Continue with template handling
           } else {
               // Ambiguous or not a template call - restore and treat as <
               restore_token_position(saved_pos);
               explicit_template_args = std::nullopt;
           }
       }
   }
   ```

2. **Test with existing test files:**
   - `tests/test_variadic_func_template.cpp`
   - Create additional test cases for edge cases

3. **Validate performance impact:**
   - Measure parsing time before and after
   - Ensure no regression on non-template code

### Phase 2: Template Registry Integration

1. **Add template function lookup:**
   ```cpp
   bool is_template_func = gTemplateRegistry.lookupAllTemplates(identifier_name) != nullptr;
   ```

2. **Use template info to guide parsing:**
   - If identifier is a known template, prefer template interpretation
   - Use template parameter count to validate argument list

### Phase 3: Comprehensive Testing

1. Test cases for:
   - Multi-argument template calls
   - Nested template arguments
   - Template calls with default arguments
   - Edge cases with operators

2. Regression testing:
   - Ensure existing functionality still works
   - Verify no performance degradation

## Impact Assessment

### Files Currently Failing

1. **tests/test_variadic_func_template.cpp**
   - Status: ⚠️ Partially working
   - Issue: Multi-argument template calls fail
   - Workaround: Use template argument deduction

### Files Fixed by Related Changes

1. **tests/test_variadic_with_members.cpp**
   - Status: ✅ Fixed
   - Issue was in pattern matching, not expression parsing

## Related Issues

- Template registry now correctly supports multiple overloads (fixed in commit 01721af)
- Partial specialization pattern matching fixed (commit a8200ff)
- Function template specializations parse correctly

## References

- C++20 Standard: Section 13.2 - "Template argument deduction"
- C++20 Standard: Section 13.10.3 - "Explicit template argument specification"
- Related commits:
  - 01721af: Fix template registry to support multiple template overloads
  - a8200ff: Fix partial specialization pattern matching for member instantiation
  - b4474c4: Address code review - add bounds checking for template parameter lookup

## Conclusion

The expression parsing issue with multi-argument template calls is a known limitation requiring careful enhancement of the lookahead mechanism. While this is the classic C++ parsing ambiguity, it can be resolved with targeted improvements to the parser's state management and disambiguation logic.

The recommended solution is to implement enhanced lookahead specifically for template argument lists, validating that parsed template arguments are followed by appropriate tokens (parentheses for function calls, double colon for qualified names) before committing to the template interpretation.
