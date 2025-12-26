# Architecture Improvement Plan for Standard Library Support

## Executive Summary

This document outlines a comprehensive plan to improve FlashCpp's architecture and implementation to enable full standard library header compilation. Based on detailed investigation (see `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`), two primary issues block progress:

1. **Variadic pack expansion in decltype base classes** (Primary blocker)
2. **Type alias resolution in expression contexts** (Secondary blocker)

## Current State Assessment

### What Works ✅
- Simple template instantiation and caching
- Conversion operators
- Constexpr control flow (loops, conditionals)
- Simple decltype base classes (non-variadic)
- Template specialization (full and partial)
- Basic variadic templates and fold expressions

### What Doesn't Work ❌
- Pack expansion in expression contexts (e.g., `decltype(__or_fn<_Bn...>(0))`)
- Type alias lookup in expression parsing
- Complex SFINAE patterns with variadic packs
- Some template metaprogramming patterns from `<type_traits>`

### Performance Status ✅
- Template instantiation is fast (20-50μs per template)
- Caching is working effectively
- Performance is NOT a blocker

---

## Issue 1: Variadic Pack Expansion in Expressions

### Problem Description

**Location**: `src/ExpressionSubstitutor.cpp`  
**Severity**: HIGH - Primary blocker for `<type_traits>`

**Failing Pattern**:
```cpp
template<typename... _Bn>
struct __or_ : decltype(__detail::__or_fn<_Bn...>(0)) { };
```

When instantiating `__or_<bool1, bool2, bool3>`:
- Need to expand `_Bn...` → `bool1, bool2, bool3`
- Need to instantiate `__or_fn<bool1, bool2, bool3>`
- Need to evaluate `decltype(__or_fn<bool1, bool2, bool3>(0))`

**Current Behavior**: `ExpressionSubstitutor` handles simple parameter substitution but doesn't recognize or expand parameter packs.

### Root Cause Analysis

1. **Pack Detection Missing**: `ExpressionSubstitutor::substituteFunctionCall()` (line 121-322) doesn't detect when template arguments contain packs
2. **No Expansion Logic**: No mechanism to expand `_Bn...` into individual arguments
3. **Instantiation Gap**: Even if expanded, need to instantiate templates with expanded arguments

### Architectural Solution

#### Phase 1: Add Pack Detection Infrastructure

**New Components Needed**:

1. **PackExpansionDetector** utility class
   ```cpp
   class PackExpansionDetector {
   public:
       // Check if a template argument is a pack expansion
       static bool isPackExpansion(const TemplateTypeArg& arg);
       
       // Check if argument list contains any packs
       static bool containsPackExpansion(const std::vector<TemplateTypeArg>& args);
       
       // Extract pack parameter names from template parameters
       static std::vector<std::string_view> extractPackNames(
           const std::vector<ASTNode>& template_params);
   };
   ```

2. **PackExpander** class for actual expansion
   ```cpp
   class PackExpander {
   public:
       // Expand a single pack into multiple arguments
       static std::vector<TemplateTypeArg> expandPack(
           std::string_view pack_name,
           const std::unordered_map<std::string_view, std::vector<TemplateTypeArg>>& pack_map);
       
       // Expand multiple packs simultaneously (for cases like f<Args1..., Args2...>)
       static std::vector<std::vector<TemplateTypeArg>> expandMultiplePacks(
           const std::vector<std::string_view>& pack_names,
           const std::unordered_map<std::string_view, std::vector<TemplateTypeArg>>& pack_map);
   };
   ```

**Files to Modify**:
- `src/ExpressionSubstitutor.h` - Add new helper methods
- `src/ExpressionSubstitutor.cpp` - Implement pack expansion logic
- `src/TemplateRegistry.h` - Add pack tracking support

#### Phase 2: Modify ExpressionSubstitutor

**Changes to `substituteFunctionCall()`**:

```cpp
ASTNode ExpressionSubstitutor::substituteFunctionCall(const FunctionCallNode& call) {
    // ... existing code ...
    
    if (call.has_template_arguments()) {
        const std::vector<ASTNode>& template_arg_nodes = call.template_arguments();
        
        // NEW: Detect if any arguments are pack expansions
        bool has_pack_expansion = false;
        std::vector<std::string_view> pack_names;
        
        for (const ASTNode& arg_node : template_arg_nodes) {
            if (isPackExpansion(arg_node)) {
                has_pack_expansion = true;
                pack_names.push_back(extractPackName(arg_node));
            }
        }
        
        if (has_pack_expansion) {
            // NEW: Expand packs
            auto expanded_args = expandPacksInArguments(template_arg_nodes, pack_names);
            
            // Instantiate template with expanded arguments
            // ... rest of expansion logic ...
        }
        
        // ... existing substitution logic ...
    }
}
```

**Implementation Steps**:

1. **Step 1.1**: Add `isPackExpansion()` check
   - Detect `...` in template argument expressions
   - Identify pack parameter names in arguments
   - Test with simple cases: `test_pack_simple.cpp`

2. **Step 1.2**: Implement single pack expansion
   - Handle `T...` → `T1, T2, T3` expansion
   - Maintain proper type information
   - Test: `test_pack_single_expansion.cpp`

3. **Step 1.3**: Handle multiple argument expansions
   - Support `f<Args1..., Args2...>`
   - Ensure correct interleaving
   - Test: `test_pack_multiple_expansion.cpp`

4. **Step 1.4**: Integrate with template instantiation
   - Trigger instantiation for each expanded template
   - Cache results appropriately
   - Test: `test_pack_instantiation.cpp`

#### Phase 3: Update Parser Integration

**Changes to `Parser::try_instantiate_class_template()`**:

Need to handle pack expansions during template instantiation:

```cpp
// In Parser.cpp, around line 24102 (deferred base class handling)
for (const auto& deferred_base : class_decl.deferred_base_classes()) {
    // Build substitution map including pack expansions
    std::unordered_map<std::string_view, TemplateTypeArg> scalar_map;
    std::unordered_map<std::string_view, std::vector<TemplateTypeArg>> pack_map;
    
    // NEW: Separate scalar and pack parameters
    for (size_t i = 0; i < template_params.size(); ++i) {
        const auto& param = template_params[i];
        if (param.is_pack()) {
            // Collect all remaining args as pack
            pack_map[param.name()] = collectPackArgs(template_args_to_use, i);
        } else {
            scalar_map[param.name()] = template_args_to_use[i];
        }
    }
    
    // Use enhanced ExpressionSubstitutor with pack support
    ExpressionSubstitutor substitutor(scalar_map, pack_map, *this);
    // ...
}
```

#### Phase 4: Testing Strategy

**Test Progression**:

1. **Unit Tests** (`tests/pack_expansion/`)
   - `test_pack_detection.cpp` - Detect pack parameters
   - `test_pack_single.cpp` - Single pack expansion
   - `test_pack_multiple.cpp` - Multiple packs
   - `test_pack_nested.cpp` - Nested pack expansions

2. **Integration Tests**
   - `test_decltype_pack_simple.cpp` - Simple decltype with pack
   - `test_decltype_pack_complex.cpp` - Complex SFINAE patterns
   - `test_type_traits_or.cpp` - Actual `__or_` pattern from `<type_traits>`

3. **Regression Tests**
   - Run full test suite after each phase
   - Ensure no existing functionality breaks

---

## Issue 2: Type Alias Resolution

### Problem Description

**Location**: `src/Parser.cpp:14393`  
**Severity**: MEDIUM - Blocks some `<type_traits>` patterns

**Failing Pattern**:
```cpp
using false_type = bool_constant<false>;

template<typename T, typename U>
struct is_same : false_type {};  // ERROR: Missing identifier: false_type
```

**Root Cause**: Expression parsing only checks `gSymbolTable`, not `gTypesByName` where type aliases are registered.

### Architectural Solution

#### Phase 1: Unified Identifier Lookup

**Problem**: Multiple lookup mechanisms without proper fallback chain:
1. `gSymbolTable.lookup()` - Variables, functions
2. `gTypesByName` - Types, type aliases
3. Template parameter lookup
4. Concept lookup

**Solution**: Create unified lookup with proper fallback chain.

**New Component**:

```cpp
// src/UnifiedLookup.h
class UnifiedIdentifierLookup {
public:
    struct LookupResult {
        enum class Kind {
            None,
            Variable,
            Function,
            Type,
            TypeAlias,
            TemplateParameter,
            Concept
        };
        
        Kind kind;
        ASTNode node;  // May be empty for types
        const TypeInfo* type_info;  // For types/aliases
    };
    
    static LookupResult lookup(
        std::string_view identifier,
        const Parser& parser);
    
private:
    static LookupResult lookupInSymbolTable(std::string_view id, const Parser& parser);
    static LookupResult lookupInTypeRegistry(std::string_view id);
    static LookupResult lookupAsTemplateParam(std::string_view id, const Parser& parser);
    static LookupResult lookupAsConcept(std::string_view id);
};
```

**Integration Points**:

1. **Parser.cpp:13365** - Replace direct `lookup_symbol()` call
   ```cpp
   // OLD:
   identifierType = lookup_symbol(StringTable::getOrInternStringHandle(idenfifier_token.value()));
   
   // NEW:
   auto lookup_result = UnifiedIdentifierLookup::lookup(idenfifier_token.value(), *this);
   identifierType = lookup_result.node;
   // Handle type aliases specially based on lookup_result.kind
   ```

2. **Context-Aware Handling** - Different actions based on where identifier is used:
   - **Base class context**: Type aliases resolve to underlying type
   - **Expression context**: Type aliases can be used as type names
   - **Template argument context**: Type aliases resolve correctly

#### Phase 2: Type Alias Context Tracking

**New Feature**: Track where an identifier is being parsed

```cpp
enum class IdentifierContext {
    Expression,
    TypeSpecifier,
    BaseClass,
    TemplateArgument,
    FunctionCall
};

class ContextTracker {
    std::vector<IdentifierContext> context_stack_;
public:
    void pushContext(IdentifierContext ctx);
    void popContext();
    IdentifierContext currentContext() const;
};
```

**Usage in Parser**:

```cpp
// In parse_primary_expression
context_tracker_.pushContext(IdentifierContext::Expression);
auto result = parse_identifier();
context_tracker_.popContext();

// In parse_base_class_list
context_tracker_.pushContext(IdentifierContext::BaseClass);
auto base_name = parse_identifier();
context_tracker_.popContext();
```

#### Phase 3: Refactor Type Alias Handling

**Changes to `parse_using_directive_or_declaration()`**:

Currently type aliases only register in `gTypesByName`. Need to also create symbol table entries for certain contexts.

```cpp
ParseResult Parser::parse_using_directive_or_declaration() {
    // ... existing code to parse type alias ...
    
    // Register in gTypesByName (existing)
    gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
    
    // NEW: Also register in symbol table for expression context access
    if (should_be_in_symbol_table(alias_type_info)) {
        auto alias_decl = createTypeAliasDeclaration(alias_token->value(), type_spec);
        gSymbolTable.add(alias_token->value(), alias_decl);
    }
    
    return saved_position.success();
}
```

#### Phase 4: Testing Strategy

**Test Cases**:

1. **Basic Type Alias Tests**
   - `test_type_alias_global.cpp` - Global scope aliases
   - `test_type_alias_namespace.cpp` - Namespace aliases
   - `test_type_alias_template.cpp` - Template member aliases

2. **Context Tests**
   - `test_alias_base_class.cpp` - Using alias as base class
   - `test_alias_expression.cpp` - Using alias in expressions
   - `test_alias_template_arg.cpp` - Using alias as template argument

3. **Regression Tests**
   - Verify existing tests still pass
   - Specific focus on tests that previously failed with attempted fix

---

## Implementation Timeline

### Phase 1: Foundation (Weeks 1-2)
- [ ] Create `PackExpansionDetector` utility class
- [ ] Add pack detection to `ExpressionSubstitutor`
- [ ] Implement `UnifiedIdentifierLookup` class
- [ ] Add basic unit tests for new utilities

### Phase 2: Pack Expansion (Weeks 3-4)
- [ ] Implement single pack expansion in `ExpressionSubstitutor`
- [ ] Handle template instantiation with expanded packs
- [ ] Test with simple decltype base class cases
- [ ] Verify no regressions in existing tests

### Phase 3: Multiple Packs (Weeks 5-6)
- [ ] Extend to multiple simultaneous pack expansions
- [ ] Handle nested pack expansions
- [ ] Test complex SFINAE patterns
- [ ] Integration testing with `<type_traits>` patterns

### Phase 4: Type Alias Resolution (Weeks 7-8)
- [ ] Integrate `UnifiedIdentifierLookup` into Parser
- [ ] Add context tracking for identifier usage
- [ ] Refactor type alias registration
- [ ] Comprehensive testing of all contexts

### Phase 5: Integration & Polish (Week 9-10)
- [ ] Full test suite validation
- [ ] Performance testing and optimization
- [ ] Documentation updates
- [ ] Test actual standard library headers

---

## Risk Mitigation

### Risk 1: Breaking Existing Functionality
**Mitigation**: 
- Incremental changes with test-driven development
- Run full test suite after each phase
- Keep old code paths until new ones are proven

### Risk 2: Performance Degradation
**Mitigation**:
- Profile before and after changes
- Use caching aggressively for pack expansions
- Optimize hot paths identified through profiling

### Risk 3: Architectural Complexity
**Mitigation**:
- Keep new classes focused and single-purpose
- Document all design decisions
- Regular code reviews of architectural changes

---

## Success Criteria

### Minimum Viable Success
1. `<type_traits>` compiles without errors
2. Basic SFINAE patterns work
3. No regressions in existing 747 tests

### Full Success
1. All standard C++ library wrapper headers compile (`<cstddef>`, `<cstdint>`, `<cstdio>`)
2. Core metaprogramming headers work (`<type_traits>`, `<utility>`)
3. Performance remains acceptable (<1s for most headers)
4. Comprehensive test coverage for new features

### Stretch Goals
1. `<vector>` and `<string>` compile (requires allocator support)
2. `<algorithm>` compiles (requires iterator concepts)
3. Full C++20 feature compatibility

---

## Code Quality Requirements

### Documentation
- All new classes must have comprehensive header comments
- Complex algorithms need inline documentation
- Update STANDARD_HEADERS_MISSING_FEATURES.md as features complete

### Testing
- Unit tests for all new utility classes
- Integration tests for each major feature
- Regression tests for all bug fixes
- Test coverage target: >90% for new code

### Performance
- No more than 10% slowdown in existing compilation times
- Pack expansion should cache aggressively
- Profile critical paths regularly

### Maintainability
- Follow existing code style (tabs, naming conventions)
- Keep functions focused (<100 lines preferred)
- Avoid deep nesting (max 4 levels)
- Use helper functions to break up complex logic

---

## Alternative Approaches Considered

### Alternative 1: Template Preprocessing
**Idea**: Expand packs during initial template parsing  
**Pros**: Simpler substitution logic  
**Cons**: Requires template re-parsing, complicates caching  
**Decision**: Rejected - runtime expansion more flexible

### Alternative 2: Multiple Symbol Tables
**Idea**: Separate symbol tables for types vs. values  
**Pros**: Clean separation of concerns  
**Cons**: Complexity in lookup, harder to maintain  
**Decision**: Rejected - unified lookup is cleaner

### Alternative 3: Lazy Type Alias Resolution
**Idea**: Don't resolve aliases until absolutely needed  
**Pros**: Simpler initial implementation  
**Cons**: Complicates later stages, hard to debug  
**Decision**: Rejected - early resolution is clearer

---

## References

- **Investigation Document**: `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`
- **ExpressionSubstitutor Implementation**: `src/ExpressionSubstitutor.h`, `src/ExpressionSubstitutor.cpp`
- **Parser Implementation**: `src/Parser.cpp`, `src/Parser.h`
- **GCC 14 `<type_traits>`**: Line 194 `__or_` pattern
- **C++20 Standard**: Pack expansion rules (§16.5.3)

---

## Appendix: Code Examples

### Example 1: Pack Expansion Detection

```cpp
// In ExpressionSubstitutor.cpp
bool ExpressionSubstitutor::isPackExpansion(const ASTNode& arg_node) {
    if (!arg_node.is<TypeSpecifierNode>()) return false;
    
    const auto& type_spec = arg_node.as<TypeSpecifierNode>();
    
    // Check if this refers to a pack parameter
    if (type_spec.type_index() < gTypeInfo.size()) {
        const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
        std::string_view type_name = StringTable::getStringView(type_info.name());
        
        // Look up in param_map to see if it's a pack
        // (This requires extending param_map to track pack status)
        return pack_param_map_.contains(type_name);
    }
    
    return false;
}
```

### Example 2: Single Pack Expansion

```cpp
std::vector<TemplateTypeArg> ExpressionSubstitutor::expandPack(
    std::string_view pack_name) {
    
    // Look up pack in pack map
    auto it = pack_param_map_.find(pack_name);
    if (it == pack_param_map_.end()) {
        return {};  // Not a pack or not found
    }
    
    // Return all expanded arguments
    return it->second;  // Vector of TemplateTypeArg
}
```

### Example 3: Unified Lookup

```cpp
UnifiedIdentifierLookup::LookupResult 
UnifiedIdentifierLookup::lookup(std::string_view identifier, const Parser& parser) {
    LookupResult result;
    
    // Try symbol table first
    auto symbol = parser.lookup_symbol(StringTable::getOrInternStringHandle(identifier));
    if (symbol.has_value()) {
        result.kind = LookupResult::Kind::Variable;  // or Function
        result.node = *symbol;
        return result;
    }
    
    // Try type registry
    auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(identifier));
    if (type_it != gTypesByName.end()) {
        result.kind = type_it->second->is_type_alias() ? 
            LookupResult::Kind::TypeAlias : LookupResult::Kind::Type;
        result.type_info = type_it->second;
        return result;
    }
    
    // Try template parameters
    // ... (existing logic)
    
    // Try concepts
    // ... (existing logic)
    
    result.kind = LookupResult::Kind::None;
    return result;
}
```

---

**Document Version**: 1.0  
**Last Updated**: December 26, 2024  
**Author**: GitHub Copilot  
**Status**: Draft for Review
