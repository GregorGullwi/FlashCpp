# C++20 Template Argument Disambiguation Plan

## Executive Summary

This document outlines a comprehensive plan to make the FlashCpp parser C++20 spec-compliant in its handling of template argument list disambiguation, specifically to support patterns like `decltype(namespace::func<Pack...>(args))`.

**Current Status**: The compiler successfully parses pack expansion in template arguments and decltype base classes, but fails to correctly disambiguate `<` as template argument delimiters vs. less-than operators in complex expression contexts.

**Goal**: Implement C++20-compliant disambiguation rules so that after qualified-ids and identifiers in expression contexts, `<` is correctly recognized as introducing template-argument-lists rather than being parsed as comparison operators.

## Problem Statement

### Current Behavior

When parsing `decltype(namespace::func<Bn...>(0))`:
1. The qualified identifier `namespace::func` is parsed
2. The `<` token is consumed by the binary operator handler as a less-than comparison
3. `Bn...` is parsed as the right-hand side of the comparison
4. Parsing fails when expecting the closing `)` for decltype

### C++20 Specification Requirements

According to C++20 §13.3 [temp.names]:
- After a `::` in a qualified-id, if the next token is `<`, it shall be taken as the delimiter of a template-argument-list
- The compiler must perform maximal munch disambiguation
- Template names followed by `<` must be treated as template-id, not as less-than comparison

### Root Cause Analysis

Through extensive debugging, we identified that:
1. ✅ The initial qualified identifier parser (line 12598) has template argument checking
2. ✅ The postfix `::` operator handler (line 14529) has template argument checking  
3. ❌ There exists a third code path for parsing qualified identifiers that doesn't check for template arguments
4. ❌ The precedence-based expression parser consumes `<` as a binary operator before disambiguation logic can run

## Architecture Analysis

### Current Parser Design

```
parse_expression(precedence)
├── parse_unary_expression()
│   ├── parse_primary_expression()
│   │   ├── Identifier lookup
│   │   └── Qualified identifier parsing (line 12598) ✓ checks for <
│   └── Postfix operator loop
│       ├── ++ / -- operators
│       ├── [] array subscript
│       ├── () function call
│       └── :: scope resolution (line 14529) ✓ checks for <
└── Binary operator loop
    ├── Precedence-based operator parsing
    └── < consumed as operator ✗ no template checking
```

### Issues with Current Design

1. **Multiple Parsing Paths**: Qualified identifiers can be created through multiple code paths, not all of which check for template arguments

2. **Precedence Conflicts**: The binary operator loop processes `<` (precedence 13) before template disambiguation can occur

3. **Context Loss**: By the time we reach template argument checking code, the parser context may have changed

4. **Early Token Consumption**: Tokens are consumed before disambiguation checks complete

## Solution Architecture

### Phase 1: Token Lookahead Enhancement (2-3 weeks)

**Goal**: Implement proper lookahead for template argument disambiguation without consuming tokens prematurely.

#### Changes Required:

1. **Create Template Argument Lookahead Function**
   ```cpp
   // New function in Parser class
   bool could_be_template_arguments(const ParseResult& left_expr);
   ```
   - Checks if current position could start template-argument-list
   - Performs speculative parsing without consuming tokens
   - Returns true if `<...>` forms valid template arguments

2. **Modify Binary Operator Handling**
   - Before treating `<` as comparison operator, check if it could be template arguments
   - Location: `parse_expression()` binary operator loop (line ~10778)
   - Add disambiguation before operator precedence check

3. **Centralize Template Argument Recognition**
   - Create single point of truth for "should we parse template arguments here?"
   - Consolidate logic from lines 12622, 14574, and new locations

#### Implementation Details:

```cpp
// In parse_expression(), before line 10778
if (peek_token()->value() == "<" && result.node().has_value()) {
    // C++20 disambiguation: check if this should be template arguments
    if (could_be_template_arguments(result)) {
        auto saved_pos = save_token_position();
        auto template_args = parse_explicit_template_arguments();
        
        if (template_args.has_value()) {
            // Successfully parsed - this was template arguments
            discard_saved_token(saved_pos);
            // Continue to check for function calls, etc.
            // DO NOT enter binary operator parsing for this <
            continue; // Skip this iteration of operator loop
        } else {
            // Failed - restore and treat as operator
            restore_token_position(saved_pos);
        }
    }
}
```

### Phase 2: Qualified Identifier Unification (3-4 weeks)

**Goal**: Consolidate all qualified identifier parsing into a single, consistent code path.

#### Current Scattered Locations:

1. Line 12598: Initial qualified identifier in `parse_primary_expression()`
2. Line 14529: Postfix `::` operator in postfix operator loop
3. Line 11983: Type parsing contexts
4. Line 12238: Using declarations
5. Line 13527: Template instantiation contexts

#### Refactoring Strategy:

1. **Create Unified Qualified Identifier Parser**
   ```cpp
   struct QualifiedIdParseResult {
       std::vector<StringType<32>> namespaces;
       Token final_identifier;
       std::optional<std::vector<TemplateTypeArg>> template_args;
       bool has_template_arguments;
   };
   
   std::optional<QualifiedIdParseResult> parse_qualified_identifier_with_templates();
   ```

2. **Replace All Qualified Identifier Parsing**
   - Audit all 8+ locations where qualified identifiers are created
   - Replace with calls to unified parser
   - Ensure template argument checking happens in all contexts

3. **Handle Context-Specific Behavior**
   - Add parameters for context (expression vs type vs declaration)
   - Return rich result structure instead of creating nodes directly
   - Let callers create appropriate nodes based on context

### Phase 3: Expression Context Refactoring (4-5 weeks)

**Goal**: Restructure expression parsing to support proper template disambiguation.

#### Key Changes:

1. **Separate Template-Aware Expression Parsing**
   ```cpp
   ParseResult parse_postfix_expression();
   ParseResult parse_template_aware_primary_expression();
   ```
   - New layer between unary and primary expression parsing
   - Handles template argument lists as part of primary expression
   - Returns complete template-id before binary operators considered

2. **Deferred Operator Parsing**
   - Don't immediately parse `<` as operator
   - Check if it could be template arguments first
   - Only parse as operator if template parsing fails

3. **Context Flags for Disambiguation**
   ```cpp
   enum class ExpressionContext {
       Normal,
       Decltype,
       TemplateArgument,
       RequiresClause,
       ConceptDefinition
   };
   ```
   - Pass context through expression parsing
   - Different disambiguation rules for different contexts
   - Decltype has strictest template-first rules

### Phase 4: Speculative Parsing Infrastructure (2-3 weeks)

**Goal**: Build robust speculative parsing for ambiguous constructs.

#### Features:

1. **Template Argument List Validation**
   - Parse `<...>` without side effects
   - Check for valid template-argument syntax
   - Verify closing `>`
   - Handle `>>` splitting for nested templates

2. **Backtracking Support**
   - Enhanced token position save/restore
   - State snapshot including symbol table lookups
   - Error message suppression during speculation

3. **Disambiguation Heuristics**
   - After qualified-id: prefer template arguments
   - After `::`: always prefer template arguments
   - In decltype: strongly prefer template arguments
   - In comparison contexts: may be operator

### Phase 5: C++20 Angle Bracket Rules (2 weeks)

**Goal**: Implement complete C++20 angle bracket disambiguation rules.

#### Rules to Implement:

1. **Maximal Munch for `>`**
   - `Foo<Bar<int>>` correctly parsed (not `> >`)
   - Already partially implemented, verify completeness

2. **Template-Name Recognition**
   - Track which identifiers are template names
   - Template names followed by `<` are always template-ids
   - Build template name database during parsing

3. **Scope Resolution Priority**
   - After `::`, `<` is always template argument delimiter
   - Even in expression contexts
   - Highest priority disambiguation rule

4. **Concept and Requires Clause Handling**
   - Special rules for concepts with template parameters
   - Requires clause expression parsing

## Implementation Roadmap

### Sprint 1-2: Foundation (Weeks 1-2)
- [ ] Implement `could_be_template_arguments()` lookahead function
- [ ] Add template argument disambiguation to binary operator loop
- [ ] Create comprehensive test suite for disambiguation cases
- [ ] Document current parser state and test existing behavior

### Sprint 3-4: Unification (Weeks 3-4)
- [ ] Design unified qualified identifier parser interface
- [ ] Audit and catalog all qualified identifier creation points
- [ ] Implement `parse_qualified_identifier_with_templates()`
- [ ] Begin migration of first 2-3 call sites

### Sprint 5-7: Migration (Weeks 5-7)
- [ ] Migrate remaining qualified identifier parsing locations
- [ ] Update postfix operator handling
- [ ] Refactor type parsing to use unified parser
- [ ] Verify no regressions in existing 731 tests

### Sprint 8-10: Expression Refactoring (Weeks 8-10)
- [ ] Implement `parse_template_aware_primary_expression()`
- [ ] Add expression context tracking
- [ ] Restructure operator precedence handling
- [ ] Handle decltype-specific disambiguation

### Sprint 11-12: Speculative Parsing (Weeks 11-12)
- [ ] Build speculative parsing infrastructure
- [ ] Implement template argument validation
- [ ] Add comprehensive backtracking support
- [ ] Performance optimization for common cases

### Sprint 13-14: C++20 Compliance (Weeks 13-14)
- [ ] Verify `>>` splitting works correctly
- [ ] Implement template name database
- [ ] Add scope resolution priority rules
- [ ] Test against GCC/Clang C++20 test suites

### Sprint 15: Testing & Polish (Week 15)
- [ ] Comprehensive testing with real-world C++20 code
- [ ] Performance profiling and optimization
- [ ] Documentation updates
- [ ] Create examples and tutorials

## Test Strategy

### Test Cases to Add

1. **Basic Template Disambiguation**
   ```cpp
   namespace ns { template<typename T> void func(); }
   void test() { ns::func<int>(); }
   ```

2. **Decltype with Template Arguments**
   ```cpp
   template<typename... Ts>
   struct test : decltype(ns::func<Ts...>(0)) {};
   ```

3. **Pack Expansion in Template Arguments**
   ```cpp
   template<typename... Args>
   using result = decltype(func<Args...>(std::declval<Args>()...));
   ```

4. **Nested Templates**
   ```cpp
   ns::outer<ns::inner<int>>::func<double>();
   ```

5. **Ambiguous Cases**
   ```cpp
   a<b>::c; // qualified-id with template arguments
   a < b > c; // comparison operators
   ```

### Regression Testing

- Maintain all 731 existing tests passing
- Add disambiguation-specific test file
- Test both success and failure cases
- Verify error messages are clear

## Performance Considerations

### Expected Impact

1. **Lookahead Cost**: Speculative parsing adds overhead
   - Mitigation: Cache results, fast-path common cases
   - Expected: <5% parser slowdown

2. **Backtracking**: Failed template argument parsing requires restore
   - Mitigation: Optimize token position save/restore
   - Use shallow copies where possible

3. **Symbol Table Lookups**: More lookups for template name checking
   - Mitigation: Add template name cache
   - Hash-based fast lookup

### Optimization Strategies

1. **Fast Path for Simple Cases**
   - If next token after `<` is obviously not template argument, skip check
   - Cache "is this a template name?" results

2. **Lazy Disambiguation**
   - Only perform full speculative parse when necessary
   - Simple heuristics first (is it a template name?)

3. **Incremental Parsing**
   - Don't re-parse already-parsed template arguments
   - Cache partial results during speculation

## Migration Strategy

### Backward Compatibility

- All existing tests must continue to pass
- Gradual rollout of new disambiguation logic
- Feature flag for new behavior during testing
- Comprehensive logging during migration

### Rollout Plan

1. **Phase 1**: New code paths only, old paths unchanged
2. **Phase 2**: Both old and new paths active, compare results
3. **Phase 3**: Switch default to new paths, old paths available
4. **Phase 4**: Remove old paths after verification period

## Risk Assessment

### High Risk Areas

1. **Performance Degradation**: Speculative parsing overhead
   - Mitigation: Performance tests, profiling
   - Acceptance: <10% slowdown acceptable

2. **Regression in Existing Code**: Breaking currently-working features
   - Mitigation: Comprehensive testing, gradual rollout
   - Acceptance: Zero regressions required

3. **Complexity Increase**: Parser becomes harder to maintain
   - Mitigation: Documentation, clear interfaces
   - Acceptance: Worth it for C++20 compliance

### Medium Risk Areas

1. **Edge Cases**: Unusual C++ constructs
   - Mitigation: Test against real-world code
   - Study GCC/Clang test suites

2. **Interaction with Other Features**: SFINAE, concepts, requires
   - Mitigation: Integration testing
   - Verify each feature independently first

## Success Criteria

### Must Have

- ✅ `decltype(namespace::func<Pack...>(args))` parses correctly
- ✅ All 731 existing tests continue to pass
- ✅ <type_traits> header compiles successfully
- ✅ No ambiguous template argument cases fail

### Should Have

- ✅ Parser performance within 10% of current
- ✅ Clear error messages for invalid template syntax
- ✅ Comprehensive documentation of new behavior
- ✅ Additional 50+ tests for disambiguation

### Nice to Have

- ✅ Compatibility with more C++20 standard headers
- ✅ Fuzzing infrastructure for parser testing
- ✅ Automatic disambiguation in more contexts

## Related Work

### C++20 Specification References

- §13.3 [temp.names]: Template names
- §13.4 [temp.arg]: Template arguments  
- §12.5 [over.oper]: Overloaded operators
- §8.5.4 [expr.post]: Postfix expressions

### Prior Art

- Clang's template argument disambiguation
- GCC's maximal munch implementation
- EDG front-end template parsing

## Conclusion

This plan provides a structured approach to achieving C++20 template disambiguation compliance. The phased implementation minimizes risk while delivering incremental value. The estimated timeline is 15 weeks for full implementation, with the most critical features (Phase 1) deliverable in 2-3 weeks.

**Next Steps**:
1. Review and approve this plan
2. Allocate development resources
3. Begin Sprint 1 with lookahead implementation
4. Track progress against roadmap

---

**Document Version**: 1.0  
**Date**: December 25, 2024  
**Author**: GitHub Copilot  
**Status**: DRAFT - Pending Review
