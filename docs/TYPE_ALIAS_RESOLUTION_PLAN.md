# Type Alias Resolution in Template Arguments - Implementation Plan

## Problem Statement

FlashCpp fails to compile standard library headers like `<type_traits>` due to "Missing identifier" errors when type aliases (like `false_type`, `true_type`) and template aliases (like `__enable_if_t`) are used in template argument contexts.

### Example Pattern That Fails

```cpp
template<typename _Tp, typename...>
using __first_t = _Tp;

template<bool _Cond, typename _Tp = void>
using __enable_if_t = typename enable_if<_Cond, _Tp>::type;

// FlashCpp fails with "Missing identifier: __enable_if_t"
template<typename... _Bn>
auto __or_fn(int) -> __first_t<false_type, __enable_if_t<!bool(_Bn::value)>...>;
```

## Root Cause Analysis

### Current FlashCpp Behavior

1. **Symbol Table Lookup**: In `parse_primary_expression()`, when an identifier is encountered, FlashCpp first looks it up in `gSymbolTable`.

2. **Type Alias Fallback**: If not found, there's a fallback to check `gTypesByName`, but only when the identifier is followed by `::` or `(`.

3. **Template Alias Registry**: Template aliases are registered in `gTemplateRegistry.alias_templates_`, but this is only checked when the identifier is followed by `<` AND after the symbol table lookup has already failed.

4. **The Gap**: When parsing template arguments like `__first_t<false_type, __enable_if_t<...>...>`:
   - `false_type` followed by `,` is not recognized as a type alias
   - `__enable_if_t` followed by `<` parses template args but then fails

### How GCC/Clang/MSVC Handle This

Major compilers use **two-phase name lookup** for templates:

1. **Phase 1 (Definition Time)**: Non-dependent names are looked up immediately
2. **Phase 2 (Instantiation Time)**: Dependent names are looked up when the template is instantiated

For type aliases in template argument contexts:
- **GCC/Clang**: Use a unified type system where type aliases are first-class citizens. When parsing template arguments, they check both the symbol table AND the type registry for identifiers.
- **MSVC**: Uses a single-phase approach but still maintains type alias visibility across all contexts.

## Proposed Solution

### Architectural Changes Required

#### 1. Unified Type/Symbol Lookup Function

Create a helper function that searches both `gSymbolTable` and `gTypesByName` consistently:

```cpp
std::optional<ASTNode> lookup_identifier_unified(
    std::string_view name, 
    ExpressionContext context,
    bool check_alias_templates = true
);
```

This function should:
- Check `gSymbolTable` first
- Fall back to `gTypesByName` for type aliases
- Check `gTemplateRegistry` for template aliases when followed by `<`
- Return appropriate node type based on what was found

#### 2. Context-Aware Type Alias Recognition

The `found_as_type_alias` check in `parse_primary_expression()` needs to be extended to recognize type aliases in ALL contexts where types are valid, not just when followed by specific tokens:

**Current check (too restrictive):**
```cpp
if (peek == "::" || peek == "(") {
    // check gTypesByName
}
```

**Proposed check:**
```cpp
// Check gTypesByName for type aliases when:
// - followed by :: (qualified name)
// - followed by ( (constructor call)  
// - in template argument context followed by , or >
// - but NOT when followed by < (could be template class, not alias)
bool should_check_type_alias = (peek == "::" || peek == "(");
if (context == ExpressionContext::TemplateArgument) {
    should_check_type_alias = should_check_type_alias || (peek == "," || peek == ">");
}
```

**Important**: Do NOT add `<` to this check, because `gTypesByName` contains ALL types (structs, classes, etc.), not just type aliases. Template aliases followed by `<` must be handled separately via `gTemplateRegistry`.

#### 3. Template Alias Handling After Argument Parsing

When `parse_explicit_template_arguments()` succeeds but the identifier wasn't in `gSymbolTable`, check if it's a template alias and handle appropriately:

```cpp
if (explicit_template_args.has_value() && !identifierType) {
    auto alias_opt = gTemplateRegistry.lookup_alias_template(identifier);
    if (alias_opt.has_value()) {
        // Create appropriate node for template alias instantiation
        // Don't fall through to "Missing identifier" error
    }
}
```

### Implementation Steps

1. **Step 1**: Add context-aware type alias lookup for `,` and `>` in template arguments
   - Modify the `should_check_type_alias` condition
   - Test with `false_type` and `true_type` in template argument lists

2. **Step 2**: Handle template aliases after argument parsing
   - Add check for alias templates after `parse_explicit_template_arguments()` succeeds
   - Ensure proper AST node creation for deferred instantiation

3. **Step 3**: Comprehensive testing
   - Ensure all existing tests pass
   - Add targeted tests for specific patterns from `<type_traits>`

### Risks and Mitigations

**Risk 1**: Adding `<` to type alias check breaks templates
- **Mitigation**: Keep template alias handling separate from simple type alias handling

**Risk 2**: Performance impact of additional lookups
- **Mitigation**: Only perform fallback lookups when primary lookup fails

**Risk 3**: Breaking existing code paths
- **Mitigation**: Run full test suite after each change

## Test Cases Needed

1. Simple type alias in template argument: `Template<false_type>`
2. Type alias with scope resolution: `false_type::value`
3. Template alias with arguments: `__enable_if_t<true>`
4. Nested template arguments: `__first_t<false_type, __enable_if_t<true>>`
5. Pack expansion with template alias: `__enable_if_t<Bs>...`

## References

- `src/Parser.cpp`: Main parsing logic, `parse_primary_expression()`
- `src/TemplateRegistry.h`: Template alias registration and lookup
- `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`: Detailed analysis of missing features
- C++20 Standard §13.8: Two-phase name lookup

## Status

**Current Status**: Planning phase - changes reverted due to test regressions

**Next Steps**: Implement changes incrementally with test validation at each step
