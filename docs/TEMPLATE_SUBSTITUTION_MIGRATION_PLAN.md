# Template Substitution Migration Plan

## Overview

This document outlines the plan to migrate template instantiation logic from `Parser.cpp` to `TemplateInstantiationHelper.h`. The migration is currently marked as "high risk" due to the deep integration with Parser state.

## Current State

### Parser Methods to Migrate

Three primary methods handle template instantiation in `Parser.cpp`:

1. **`try_instantiate_class_template()`** (~500 lines)
   - Handles class/struct template instantiation
   - Creates instantiated types in `gTypesByName` and `gTypeInfo`
   - Processes base classes, member variables, member functions
   - Handles partial and full specializations

2. **`try_instantiate_function_template()`** (~300 lines)
   - Handles function template instantiation
   - Creates instantiated functions in the function tables
   - Handles overload resolution with template argument deduction

3. **`try_instantiate_variable_template()`** (~150 lines)
   - Handles variable template instantiation
   - Creates instantiated variables in the symbol tables

### Dependencies on Parser State

The instantiation methods depend on these Parser-internal systems:

1. **Token Stream**
   - Re-parsing template bodies requires access to saved token positions
   - `saved_token_position_` and `current_position_` are used to rewind

2. **Symbol Tables**
   - `scope_stack_` for nested scope resolution
   - `variable_declarations_` for variable lookup
   - `function_declarations_` for function lookup

3. **Type System**
   - `gTypesByName` and `gTypeInfo` (global, accessible)
   - `type_specifier_stack_` for nested type resolution

4. **Template System**
   - `template_arguments_` stack for nested instantiations
   - `template_parameters_` for parameter lookup
   - Recursion guards to prevent infinite instantiation

## Risk Assessment

### Why Migration is High Risk

1. **900+ Passing Tests**: Current implementation passes extensive test suite
2. **Deep Parser Integration**: Methods read/write Parser-private state
3. **Complex Control Flow**: Many early returns, error paths, recursive calls
4. **Subtle Semantics**: C++ template instantiation has many corner cases

### What Could Go Wrong

- Breaking template argument deduction
- Breaking partial specialization matching
- Breaking SFINAE behavior
- Breaking nested template instantiation
- Breaking default template arguments
- Performance regression from additional indirection

## Investigation Plan

### Phase A: Code Analysis (Low Risk)

1. **Map Dependencies**
   - List all Parser member variables accessed by each instantiation method
   - Categorize as: required (must pass), optional (can compute), removable

2. **Identify Extraction Candidates**
   - Pure functions that don't access Parser state
   - Functions that only access global state (gTypeInfo, etc.)
   - Functions that could accept Parser state as parameters

3. **Document Test Coverage**
   - Map each instantiation code path to test files that cover it
   - Identify gaps in test coverage

### Phase B: Extract Helper Functions (Medium Risk)

Extract pure functions first (no Parser state dependency):

1. **`substitute_type_in_node()`** - Already in TemplateInstantiationHelper
2. **`build_substituted_type_name()`** - String manipulation only
3. **`check_template_constraints()`** - Concept checking logic
4. **`match_partial_specialization()`** - Pattern matching logic

### Phase C: Create Interface Layer (Low Risk)

Pass `Parser&` directly to instantiation functions - no virtual functions needed:

```cpp
// TemplateInstantiationHelper.h
TypeIndex instantiateClassTemplate(Parser& parser, const ASTNode& template_decl, 
                                    const std::vector<TemplateTypeArg>& args);
TypeIndex instantiateFunctionTemplate(Parser& parser, const std::string& name,
                                     const std::vector<TemplateTypeArg>& args);
TypeIndex instantiateVariableTemplate(Parser& parser, const std::string& name,
                                      const std::vector<TemplateTypeArg>& args);

// Parser.cpp - thin wrappers
TypeIndex Parser::try_instantiate_class_template(/* args */) {
    return ::instantiateClassTemplate(*this, /* args */);
}
```

### Phase D: Incremental Migration (High Risk)

Migrate one method at a time:

1. **Variable Templates First** (smallest, simplest)
   - ~150 lines
   - Fewer dependencies
   - Lower risk of breaking other features

2. **Function Templates Second**
   - ~300 lines
   - More complex due to overloading
   - Well-tested via STL-like tests

3. **Class Templates Last** (largest, most complex)
   - ~500 lines
   - Most Parser dependencies
   - Highest risk

## Detailed Task Breakdown

### Task 1: Dependency Analysis (2-4 hours)

```
For each Parser method involved in template instantiation:
1. List all `this->` member variable accesses
2. List all global variable accesses (gTypeInfo, etc.)
3. List all helper method calls
4. Create dependency graph
```

**Output**: `docs/TEMPLATE_INSTANTIATION_DEPENDENCIES.md`

### Task 2: Extract Pure Functions (4-8 hours)

Functions to extract (no Parser state):

```cpp
// Already extracted:
buildTemplateParamMap()
buildTemplateArgumentsFromTypeArgs()
generateInstantiatedNameFromArgs()

// To extract:
matchPartialSpecialization(template_params, args, specialization_pattern)
checkTemplateConstraints(template_params, args, constraints)
substituteTypeInExpression(expr, param_to_arg_map)
```

### Task 3: Function Signatures (1-2 hours)

Design function signatures in `TemplateInstantiationHelper.h`:
- Pass `Parser&` as first parameter
- Identify all Parser member accesses needed
- Keep Parser methods as thin wrappers

### Task 4: Variable Template Migration (4-8 hours)

1. Extract `instantiate_variable_template()` core logic
2. Keep Parser method as thin wrapper
3. Test with all variable template tests
4. Measure performance impact

### Task 5: Function Template Migration (8-16 hours)

1. Extract `instantiate_function_template()` core logic
2. Handle template argument deduction complexity
3. Test with all function template tests

### Task 6: Class Template Migration (16-32 hours)

1. Extract `instantiate_class_template()` core logic
2. Handle member instantiation recursion
3. Handle base class instantiation
4. Test with all class template tests

## Success Criteria

1. **All 900+ tests pass** after migration
2. **No performance regression** (measure compile times)
3. **Cleaner separation** between parsing and instantiation
4. **Better testability** of instantiation logic in isolation

## Rollback Plan

Each phase should be a separate commit/PR:
1. If tests fail after extraction, revert that commit
2. Keep Parser methods as fallback during migration
3. Use feature flag to switch between old/new implementation

## Timeline Estimate

| Phase | Estimated Time | Risk Level |
|-------|----------------|------------|
| A: Analysis | 2-4 hours | Low |
| B: Extract Helpers | 4-8 hours | Medium |
| C: Function Signatures | 1-2 hours | Low |
| D1: Variable Templates | 4-8 hours | Low |
| D2: Function Templates | 8-16 hours | Low |
| D3: Class Templates | 16-32 hours | Medium |
| **Total** | **35-70 hours** | - |

## Recommendation

Given the simplified approach using `Parser&` directly:

1. **Start with Phase A (Analysis)** - Safe, provides valuable information
2. **Continue with Phase B (Extract Helpers)** - Low risk, immediate benefit
3. **Proceed with Phase C & D** - Risk reduced by eliminating virtual function overhead

Using `Parser&` directly instead of virtual functions:
- Zero runtime overhead
- Simpler code structure
- No interface indirection
- Parser methods become thin wrappers

The migration can proceed more confidently given the reduced complexity.
