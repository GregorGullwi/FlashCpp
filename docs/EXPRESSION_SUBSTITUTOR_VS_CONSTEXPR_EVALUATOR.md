# ExpressionSubstitutor vs ConstExpr::Evaluator - Analysis and Consolidation Plan

## Executive Summary

**Question:** Are ExpressionSubstitutor and ConstExpr::Evaluator doing similar things? Can they be consolidated?

**Answer:** They serve **different but complementary purposes** and should **not** be merged. However, there are opportunities for better integration and code reuse.

## Purpose Comparison

### ExpressionSubstitutor
**Purpose:** Template parameter **substitution** during template instantiation
- **When:** During template class/function instantiation (compile-time, before evaluation)
- **What:** Replaces template parameter references (T, Args...) with concrete types (int, std::string, etc.)
- **Input:** Expression AST with template parameters
- **Output:** Expression AST with concrete types
- **Example:** `decltype(base_trait<T>())` → `decltype(base_trait<int>())`

### ConstExpr::Evaluator
**Purpose:** Constant expression **evaluation** 
- **When:** During constexpr evaluation (static_assert, constexpr variables, array sizes)
- **What:** Computes the runtime value of constant expressions
- **Input:** Expression AST (may contain template function calls)
- **Output:** Integer/bool/double value
- **Example:** `2 + 3` → `5`, `func(5)` → `true`

## Key Differences

| Aspect | ExpressionSubstitutor | ConstExpr::Evaluator |
|--------|----------------------|---------------------|
| **Operation** | AST transformation | Value computation |
| **Phase** | Template instantiation | Constexpr evaluation |
| **Input** | AST with template params | AST with concrete types |
| **Output** | Modified AST | Primitive value (int/bool/double) |
| **Depends on** | Template parameter map | Symbol table, parser (for instantiation) |
| **Handles** | Type substitution | Arithmetic, function calls, type traits |

## Current Integration Points

### Where They Work Together

1. **Template Function Instantiation (My Recent Work)**
   - ExpressionSubstitutor: Called during template instantiation to substitute template params
   - ConstExpr::Evaluator: May trigger template function instantiation if needed during evaluation
   - **Flow:** Evaluator detects template function → asks Parser to instantiate → Parser uses ExpressionSubstitutor

2. **Static Assert in Template Bodies**
   - ExpressionSubstitutor: Substitutes template params in the static_assert condition
   - ConstExpr::Evaluator: Evaluates the substituted expression
   - **Flow:** Parser defers static_assert → instantiation substitutes params → evaluation computes result

## Current Architecture (Simplified)

```
Template Instantiation Path:
  Parser.instantiate_template()
    → ExpressionSubstitutor.substitute()  // Replace T with int
      → Modified AST

Constexpr Evaluation Path:
  Parser.parse_static_assert()
    → ConstExpr::Evaluator.evaluate()     // Compute value
      → If template function: Parser.try_instantiate_template_explicit()
        → ExpressionSubstitutor.substitute()  // Used internally
      → Primitive value
```

## Areas of Overlap (Potential Issues)

### 1. Template Function Call Handling

**Current State:** Both handle template function calls, but differently:

- **ExpressionSubstitutor.substituteFunctionCall():**
  - Lines 407-473 in ExpressionSubstitutor.cpp
  - Looks up template functions
  - Deduces template arguments from constructor call patterns
  - Instantiates templates via Parser.try_instantiate_template_explicit()
  - Returns modified AST

- **ConstExpr::Evaluator.evaluate_function_call():**
  - Lines 1249-1447 in ConstExprEvaluator.h
  - Looks up template functions (with namespace prefix fallback)
  - Deduces template arguments from constructor call patterns
  - Instantiates templates via Parser.try_instantiate_template_explicit()
  - Evaluates the instantiated function

**Duplication:** Template argument deduction logic is duplicated!

### 2. Constructor Call Handling

Both classes handle `Type{}` patterns:
- **ExpressionSubstitutor:** Substitutes template params in the type
- **ConstExpr::Evaluator:** Returns default value (0, false, etc.)

**This is correct** - they do different things with the same pattern.

## Consolidation Opportunities

### Option 1: Extract Common Template Function Instantiation Logic ✅ RECOMMENDED

**What:** Create a shared helper for template argument deduction and instantiation

**Benefits:**
- Eliminates code duplication
- Ensures consistent behavior
- Easier to maintain and enhance

**Proposed Structure:**
```cpp
// New file: src/TemplateInstantiationHelper.h
class TemplateInstantiationHelper {
public:
    // Deduce template arguments from function call arguments
    static std::vector<TemplateTypeArg> deduceTemplateArgsFromCall(
        const ChunkedVector<ASTNode>& arguments);
    
    // Try to instantiate a template function with deduced or explicit args
    static std::optional<ASTNode> instantiateTemplateFunction(
        Parser& parser,
        std::string_view func_name,
        const std::vector<TemplateTypeArg>& template_args);
};
```

**Impact:**
- Refactor ExpressionSubstitutor.substituteFunctionCall()
- Refactor ConstExpr::Evaluator.evaluate_function_call()
- Both call TemplateInstantiationHelper methods

**Effort:** Medium (1-2 days)
**Risk:** Low (well-defined interfaces)

### Option 2: Make ConstExpr::Evaluator Use ExpressionSubstitutor ❌ NOT RECOMMENDED

**What:** Have evaluator call substitutor before evaluation

**Problems:**
- Evaluator doesn't have template parameter maps (operates on already-substituted expressions)
- Would require passing around substitution context unnecessarily
- Violates separation of concerns (evaluation shouldn't know about substitution)

### Option 3: Merge into Single Class ❌ NOT RECOMMENDED

**What:** Combine both classes into TemplateExpressionHandler

**Problems:**
- Violates single responsibility principle
- Makes code harder to understand and test
- Different use cases require different interfaces
- Would make the codebase less modular

## Recommended Action Plan

### Phase 1: Create Shared Helper (High Priority)
1. Create `src/TemplateInstantiationHelper.h/cpp`
2. Extract template argument deduction logic:
   - Move from ExpressionSubstitutor.cpp lines 420-447
   - Move from ConstExprEvaluator.h lines 1390-1416
   - Create unified `deduceTemplateArgsFromCall()` method

3. Extract template instantiation logic:
   - Common pattern for trying multiple name variations
   - Unified error handling

4. Update callers:
   - ExpressionSubstitutor.substituteFunctionCall()
   - ConstExpr::Evaluator.evaluate_function_call()

### Phase 2: Better Integration (Medium Priority)
1. Document the flow between classes in code comments
2. Add integration tests that exercise both paths
3. Consider adding debug logging to trace substitution → evaluation flow

### Phase 3: Future Enhancements (Low Priority)
1. Template argument deduction from function parameter types (not just constructor patterns)
2. Support for template template parameters
3. Better error messages when instantiation fails

## Testing Strategy

### Unit Tests
- Test TemplateInstantiationHelper in isolation
- Verify deduction works for various patterns:
  - `func(__type_identity<int>{})`
  - `func(wrapper<int, double>{})`
  - Multiple arguments with different patterns

### Integration Tests
- Template function in static_assert (exercises both classes)
- Template function in decltype base class (exercises ExpressionSubstitutor)
- Template function in constexpr variable init (exercises ConstExpr::Evaluator)

### Regression Tests
- Ensure type_traits header progress isn't lost
- Verify existing template instantiation still works

## Conclusion

**Short Answer:** No, don't consolidate. They do different things.

**But:** Extract the duplicated template function instantiation logic into a shared helper.

This will:
- ✅ Reduce code duplication
- ✅ Improve maintainability  
- ✅ Ensure consistent behavior
- ✅ Make future enhancements easier
- ✅ Preserve separation of concerns

**Estimated Effort:** 2-3 days for Phase 1, with testing
**Risk Level:** Low - well-defined refactoring with clear boundaries
