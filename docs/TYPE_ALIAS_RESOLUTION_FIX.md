# Type Alias Resolution Fix - December 26, 2024

## Problem
The FlashCpp compiler was failing to compile standard library headers with "Missing identifier" errors for type aliases like `false_type`, `true_type`, and others when used in template argument expression contexts.

### Specific Errors (from `<type_traits>` compilation):
```
[ERROR][Parser] Missing identifier: false_type
[ERROR][Parser] Missing identifier: true_type
```

## Root Cause
Type aliases defined with `using` declarations were registered in `gTypesByName` but not in `gSymbolTable`. When the parser encountered these identifiers in expression contexts (particularly within template argument lists), it only checked `gSymbolTable` and failed with "Missing identifier" errors.

The existing fallback check for `gTypesByName` (around line 13687) only handled constructor call syntax (identifiers followed by `(`), not general identifier usage in expressions.

## Solution
Added a fallback check to `gTypesByName` in `parse_primary_expression()` (Parser.cpp, line ~14467) before reporting "Missing identifier" error. When an identifier is not found in `gSymbolTable` and is not a pack expansion, the parser now checks if it exists in `gTypesByName` as a type alias.

### Code Change
**File:** `src/Parser.cpp`
**Location:** Line ~14467

```cpp
// Before erroring on "Missing identifier", check if this is a type alias
if (!identifierType) {
    auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(idenfifier_token.value()));
    if (type_it != gTypesByName.end()) {
        // Found a type alias - treat it as an identifier reference to the type
        FLASH_LOG(Parser, Debug, "Identifier '", idenfifier_token.value(), 
                  "' found in gTypesByName, treating as type alias reference");
        result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
        return ParseResult::success(*result);
    }
    
    // Still not found - this is an error
    FLASH_LOG(Parser, Error, "Missing identifier: ", idenfifier_token.value());
    return ParseResult::error("Missing identifier", idenfifier_token);
}
```

## Results

### ✅ Fixed Errors
- `Missing identifier: false_type` - **RESOLVED**
- `Missing identifier: true_type` - **RESOLVED**

### ✅ Standard Library Header Progress
- **`<type_traits>`**: Now parses to line 258 (previously failed at line ~175)
- **`<cstddef>`**: Still compiles successfully (no regression)
- **`<cstdio>`**: Still compiles successfully (no regression)
- **`<cstdint>`**: Still compiles successfully (no regression)

### ✅ Test Suite Status
- **752/758 tests pass** (no regressions introduced)
- Test failures are pre-existing issues unrelated to this change

### Test Cases Added
1. `test_type_alias_in_expression_ret42.cpp` - Basic type alias resolution ✅
2. `test_type_alias_fix_simple_ret42.cpp` - Template argument usage ✅
3. `test_type_alias_template_arg_ret42.cpp` - Default template parameters ✅
4. `test_type_alias_in_sfinae_ret42.cpp` - SFINAE patterns ✅
5. `test_type_alias_as_base_ret42.cpp` - Base class usage ✅
6. `test_type_alias_resolution_fix_ret42.cpp` - Complex patterns ✅

## Impact
This fix removes a significant blocker for standard library header compilation. Type aliases can now be used in:
- Template argument lists
- Expression contexts
- SFINAE patterns
- Default template parameters

This enables more advanced template metaprogramming patterns from the standard library to parse correctly.

## Related Documentation
- `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md` - Original issue documentation (lines 1121-1147)
- Document section: "Secondary Issue: Type Alias Resolution"

## Future Work
The following issues remain and could be addressed in future updates:

1. **`__enable_if_t` errors**: These are non-fatal errors that occur during template argument backtracking. The parser tries to parse the identifier as an expression, fails, then successfully parses it as a type. These errors can be reduced by improving the template argument parsing heuristics.

2. **Pack expansion in decltype base classes**: More complex feature requiring changes to `ExpressionSubstitutor` (documented as "Primary blocker" in STANDARD_HEADERS_MISSING_FEATURES.md).

3. **Advanced base class patterns**: Some advanced patterns like using template parameters as base classes need additional support.

## References
- Commit: bacd055
- Branch: copilot/start-missing-features-implementation
- Date: December 26, 2024
