# AST Walker Implementation Plan for Template-Dependent decltype Base Classes

## Executive Summary

This document outlines the implementation plan for completing template-dependent `decltype` base class support in FlashCpp. The goal is to enable patterns like:

```cpp
template<typename T>
struct wrapper : decltype(base_trait<T>()) { };
```

This pattern is used extensively in standard library headers, particularly `<type_traits>` at line 194.

## Current State

### ✅ What Works
- Non-template structs with decltype bases: `struct test : decltype(get_result()) { };`
- Parsing and deferral of template-dependent decltype expressions
- Substitution map infrastructure during template instantiation
- Error messages indicating when substitution is needed

### ⚠️ What's Missing
Template-dependent decltype expressions cannot be fully evaluated because:
1. Template parameters (e.g., `T`) in nested expressions aren't substituted
2. Template instantiations within expressions (e.g., `base_trait<T>`) aren't triggered
3. The expression AST isn't updated with instantiated types

## The Challenge

Consider this template instantiation:
```cpp
template<typename T>
struct wrapper : decltype(base_trait<T>()) { };

wrapper<int> w;  // Instantiate with T=int
```

**What needs to happen:**
1. During `wrapper<int>` instantiation, process the decltype base class
2. The stored expression is: `ConstructorCallNode(base_trait<T>)`
3. Need to substitute `T` → `int` to get: `ConstructorCallNode(base_trait<int>)`
4. Instantiate `base_trait<int>` if not already instantiated
5. Update the expression to reference the instantiated type
6. Evaluate the updated expression to get the base class type

## Architecture Overview

### New Components

#### 1. Expression AST Walker
**Location**: `src/ExpressionSubstitutor.h` (new file)

**Purpose**: Traverse expression AST nodes and perform template parameter substitution

**Key Functions**:
```cpp
class ExpressionSubstitutor {
public:
    // Main entry point
    ASTNode substitute(
        const ASTNode& expr,
        const std::vector<ASTNode>& template_params,
        const std::vector<TemplateTypeArg>& template_args,
        Parser& parser);

private:
    // Handlers for different expression types
    ASTNode substituteConstructorCall(const ConstructorCallNode& ctor);
    ASTNode substituteFunctionCall(const FunctionCallNode& call);
    ASTNode substituteBinaryOp(const BinaryOperatorNode& binop);
    ASTNode substituteUnaryOp(const UnaryOperatorNode& unop);
    ASTNode substituteQualifiedId(const QualifiedIdentifierNode& qual);
    
    // Helper: substitute in a type specifier with template args
    TypeSpecifierNode substituteInTemplatedType(const TypeSpecifierNode& type);
    
    // Helper: trigger template instantiation for a type
    void ensureTemplateInstantiated(
        std::string_view template_name,
        const std::vector<TemplateTypeArg>& args);
    
    // Substitution context
    std::unordered_map<std::string_view, TemplateTypeArg> param_map_;
    Parser& parser_;
};
```

#### 2. Template Argument Substitution in Types
**Location**: Extend `src/Parser.cpp` (new functions)

**Purpose**: Handle substitution within type specifiers that have template arguments

**Key Functions**:
```cpp
// Extract template arguments from a templated type name
// Example: "base_trait<T>" → template_name="base_trait", args=[T]
std::optional<std::pair<std::string_view, std::vector<TemplateTypeArg>>>
Parser::extract_template_info_from_type(const TypeSpecifierNode& type);

// Substitute template parameters in a list of template arguments
std::vector<TemplateTypeArg> Parser::substitute_in_template_args(
    const std::vector<TemplateTypeArg>& args,
    const std::unordered_map<std::string_view, TemplateTypeArg>& param_map);
```

#### 3. Integration Point
**Location**: `src/Parser.cpp::try_instantiate_class_template()`

**Current code** (line ~24082):
```cpp
if (deferred_base.decltype_expression.is<ExpressionNode>()) {
    // Build substitution map
    // TODO: Substitute and evaluate
    auto type_spec_opt = get_expression_type(deferred_base.decltype_expression);
    // ...
}
```

**Updated code**:
```cpp
if (deferred_base.decltype_expression.is<ExpressionNode>()) {
    // Build substitution map
    std::unordered_map<std::string_view, TemplateTypeArg> param_map;
    // ... (existing map building code)
    
    // NEW: Use ExpressionSubstitutor
    ExpressionSubstitutor substitutor(param_map, *this);
    ASTNode substituted_expr = substitutor.substitute(
        deferred_base.decltype_expression,
        template_params,
        template_args_to_use);
    
    // Evaluate the substituted expression
    auto type_spec_opt = get_expression_type(substituted_expr);
    // ...
}
```

## Implementation Plan

### Phase 1: Core AST Walker Infrastructure (2-3 days)
**Goal**: Create the basic expression traversal mechanism

**Tasks**:
1. Create `ExpressionSubstitutor.h` with class skeleton
2. Implement basic traversal for simple expression types:
   - Literals (return as-is)
   - Identifiers (check if it's a template parameter)
   - Binary operators (recurse on left and right)
   - Unary operators (recurse on operand)
3. Add unit tests for simple cases

**Deliverable**: Can substitute template parameters in simple arithmetic expressions

**Test Case**:
```cpp
template<typename T, T value>
struct test : decltype(value + 1) { };  // Simple, no nested templates
```

### Phase 2: Constructor Call Substitution (3-4 days)
**Goal**: Handle constructor calls with template arguments

**Tasks**:
1. Implement `substituteConstructorCall()`
2. Add logic to:
   - Extract template name and arguments from the type
   - Check if template arguments contain template parameters
   - Substitute parameters in template arguments
   - Instantiate the template with substituted arguments
   - Return updated ConstructorCallNode with instantiated type
3. Add comprehensive tests

**Deliverable**: Can handle `decltype(base_trait<T>())`

**Test Case**:
```cpp
template<typename T>
struct wrapper : decltype(base_trait<T>()) { };
```

### Phase 3: Function Call Substitution (2-3 days)
**Goal**: Handle function calls with explicit template arguments

**Tasks**:
1. Implement `substituteFunctionCall()`
2. Handle function templates with explicit template arguments
3. Trigger function template instantiation when needed
4. Add tests

**Deliverable**: Can handle `decltype(get_trait<T>())`

**Test Case**:
```cpp
template<typename T>
struct wrapper : decltype(detail::get_trait<T>()) { };
```

### Phase 4: Qualified Identifiers (2 days)
**Goal**: Handle namespace-qualified and member access

**Tasks**:
1. Implement `substituteQualifiedId()`
2. Handle cases like `std::decay<T>::type`
3. Add tests for nested templates

**Deliverable**: Can handle complex qualified expressions

**Test Case**:
```cpp
template<typename T>
struct wrapper : decltype(std::decay<T>::type{}) { };
```

### Phase 5: Integration and Testing (3-4 days)
**Goal**: Integrate with existing codebase and test thoroughly

**Tasks**:
1. Update `try_instantiate_class_template()` to use the new substitutor
2. Add logging at debug level for troubleshooting
3. Run full test suite and fix any regressions
4. Test with real `<type_traits>` patterns
5. Performance testing (ensure no significant slowdown)

**Deliverable**: Full integration with all tests passing

### Phase 6: Documentation and Polish (1-2 days)
**Goal**: Document the implementation and edge cases

**Tasks**:
1. Update `DECLTYPE_BASE_IMPLEMENTATION.md` with completion notes
2. Add inline code comments explaining complex logic
3. Document known limitations (if any remain)
4. Update `STANDARD_HEADERS_MISSING_FEATURES.md`

**Total Estimated Time**: 13-18 days

## Technical Details

### Expression Type Handling

Different expression types require different substitution strategies:

#### ConstructorCallNode
```cpp
// Input: base_trait<T>() where T is a template parameter
// Process:
// 1. Extract type: base_trait<T>
// 2. Parse template args: [T]
// 3. Substitute: T → int
// 4. Instantiate: base_trait<int>
// 5. Create new ConstructorCallNode with base_trait<int> type
```

#### FunctionCallNode
```cpp
// Input: get_trait<T>() where T is a template parameter
// Process:
// 1. Check if function has explicit template args
// 2. Extract template args from mangled name or AST
// 3. Substitute template parameters
// 4. Instantiate function template with new args
// 5. Update FunctionCallNode to reference instantiated function
```

#### BinaryOperatorNode / UnaryOperatorNode
```cpp
// Input: expr1 + expr2
// Process:
// 1. Recursively substitute in left operand
// 2. Recursively substitute in right operand
// 3. Create new BinaryOperatorNode with substituted operands
```

### Template Argument Representation

Template arguments in type names need special handling:

**Problem**: A type like `base_trait<T>` is represented as a single TypeSpecifierNode, but we need to extract and manipulate the template arguments.

**Solution**: Parse the type name string to extract template information:
```cpp
struct TemplateTypeInfo {
    std::string_view base_name;          // "base_trait"
    std::vector<std::string_view> args;  // ["T"]
    bool has_template_args;              // true
};

TemplateTypeInfo parseTemplateType(std::string_view type_name);
```

### Handling Variadic Templates

Special consideration for variadic templates:
```cpp
template<typename... Ts>
struct test : decltype(__or_fn<Ts...>(0)) { };
```

**Challenges**:
- Pack expansion in template arguments
- Multiple template arguments from a single pack

**Solution**: Track which parameters are packs and expand them appropriately

## Edge Cases and Considerations

### 1. Circular Dependencies
**Problem**: `A<T>` instantiation depends on `B<T>`, which depends on `A<T>`

**Solution**: Track instantiation stack and detect cycles, return error with clear message

### 2. SFINAE Failures
**Problem**: Substitution might fail intentionally (SFINAE)

**Solution**: Catch substitution failures gracefully, treat as decltype evaluation failure

### 3. Dependent Types in Nested Contexts
**Problem**: `decltype(typename T::type::value_type())`

**Solution**: Extend substitutor to handle dependent type names (requires `typename` keyword support)

### 4. Multiple Template Parameters
**Problem**: `decltype(combine<T, U>())`

**Solution**: Substitution map handles multiple parameters naturally

### 5. Default Template Arguments
**Problem**: `base_trait<T>` where `base_trait` has default arguments

**Solution**: Use existing default argument filling logic in template instantiation

## Testing Strategy

### Unit Tests
Create focused tests for each component:
- `test_expression_substitutor_literals.cpp`
- `test_expression_substitutor_identifiers.cpp`
- `test_expression_substitutor_constructor.cpp`
- `test_expression_substitutor_function.cpp`
- `test_expression_substitutor_binary.cpp`

### Integration Tests
Test full decltype base class scenarios:
- `test_decltype_base_template_simple.cpp` - Single template parameter
- `test_decltype_base_template_multiple.cpp` - Multiple parameters
- `test_decltype_base_template_nested.cpp` - Nested templates
- `test_decltype_base_template_qualified.cpp` - Qualified names
- `test_decltype_base_template_sfinae.cpp` - SFINAE cases

### Standard Library Tests
Test actual patterns from standard headers:
- Extract simplified versions of `<type_traits>` patterns
- Test `__or_`, `__and_`, `__not_` metafunctions
- Verify compatibility with GCC/Clang patterns

## Performance Considerations

### Optimization Opportunities
1. **Cache instantiated templates**: Don't re-instantiate if already done
2. **Early exit**: If no template parameters in expression, skip substitution
3. **Lazy evaluation**: Only instantiate templates when actually needed

### Expected Impact
- Most code unaffected (non-template structs skip this path entirely)
- Template instantiation already cached
- Expected overhead: <5% on template-heavy code

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Can handle `decltype(base_trait<T>())` pattern
- [ ] Passes all existing 747 tests
- [ ] New tests covering basic template-dependent decltype bases pass

### Full Success
- [ ] Can handle all `<type_traits>` decltype patterns
- [ ] Handles variadic templates in decltype
- [ ] Handles SFINAE cases gracefully
- [ ] No measurable performance regression (<5%)
- [ ] Comprehensive documentation

### Stretch Goals
- [ ] Support `decltype(auto)` in base classes
- [ ] Support dependent member types: `typename T::type`
- [ ] Support pack expansion in base classes

## Risks and Mitigation

### Risk 1: Complexity Explosion
**Risk**: AST walking with template instantiation could become very complex

**Mitigation**: 
- Start with simple cases, add complexity incrementally
- Extensive unit testing at each stage
- Clear separation of concerns (walking vs. substitution vs. instantiation)

### Risk 2: Performance Impact
**Risk**: Expression traversal might slow down template instantiation

**Mitigation**:
- Profile before and after implementation
- Add caching where appropriate
- Optimize hot paths

### Risk 3: Incomplete Coverage
**Risk**: Some edge cases might not be handled

**Mitigation**:
- Document known limitations clearly
- Provide workarounds for unsupported cases
- Plan for iterative improvements

## Next Steps

1. **Review this plan** with stakeholders
2. **Set up development branch** for AST walker work
3. **Begin Phase 1** implementation
4. **Schedule regular check-ins** to assess progress and adjust plan

## References

- Current implementation: `src/Parser.cpp` lines 24079-24135
- Existing substitution function: `src/Parser.cpp` lines 22625-22724
- Standard library patterns: `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`
- Design discussion: `DECLTYPE_BASE_IMPLEMENTATION.md`

## Revision History

- 2024-12-26: Initial planning document created
