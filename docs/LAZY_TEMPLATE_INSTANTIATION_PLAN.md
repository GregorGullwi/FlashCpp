# Lazy Template Instantiation Implementation Plan

This document describes the current state of lazy template instantiation in FlashCpp and proposes improvements to reduce memory usage and improve compilation times for template-heavy code like the C++ standard library headers.

## Current Implementation Status

### ✅ Already Implemented: Member Function Lazy Instantiation

**What it does:** When a template class is instantiated (e.g., `std::vector<int>`), member functions are NOT immediately instantiated. Instead, they are registered for lazy instantiation and only instantiated when actually called.

**Location:** `src/TemplateRegistry.h` - `LazyMemberInstantiationRegistry` class

**How it works:**
1. During class template instantiation in `Parser::instantiate_class_template()` (around line 36196)
2. If `use_lazy_instantiation` is true, member functions with definitions are registered in `LazyMemberInstantiationRegistry`
3. When a member function call is encountered, the parser checks if lazy instantiation is needed via `LazyMemberInstantiationRegistry::needsInstantiation()`
4. If needed, the function is instantiated on-demand via `Parser::instantiateLazyMemberFunction()`

**Controlled by:** `--eager-template-instantiation` flag (lazy is default)

### ❌ Not Yet Implemented

The following components are currently instantiated eagerly (immediately) and could benefit from lazy instantiation:

1. **Static Members** - Instantiated immediately during class template instantiation
2. **Whole Template Classes** - Instantiated immediately when the type is used
3. **Type Aliases in Templates** - Evaluated immediately
4. **Nested Types in Templates** - Instantiated immediately

## Proposed Improvements

### Phase 1: Static Member Lazy Instantiation

**Goal:** Defer instantiation of static data members until they are actually accessed (ODR-used).

**Current Behavior:**
- In `Parser::instantiate_class_template()` around line 35561
- All static members are copied and their initializers substituted immediately
- This can trigger significant template expansion for complex static members

**Proposed Changes:**

1. **Create `LazyStaticMemberRegistry`** (new class in `TemplateRegistry.h`):
```cpp
struct LazyStaticMemberInfo {
    StringHandle class_template_name;      // e.g., "integral_constant"
    StringHandle instantiated_class_name;  // e.g., "integral_constant_bool_true"
    StringHandle member_name;              // e.g., "value"
    StructStaticMember original_member;    // Original static member definition
    std::vector<ASTNode> template_params;  // Template parameters
    std::vector<TemplateTypeArg> template_args; // Concrete arguments
    bool needs_initialization;             // Has initializer?
};

class LazyStaticMemberRegistry {
public:
    static LazyStaticMemberRegistry& getInstance();
    void registerLazyStaticMember(LazyStaticMemberInfo info);
    bool needsInstantiation(StringHandle class_name, StringHandle member_name) const;
    std::optional<LazyStaticMemberInfo> getLazyMemberInfo(StringHandle class_name, StringHandle member_name);
    void markInstantiated(StringHandle class_name, StringHandle member_name);
};
```

2. **Modify `instantiate_class_template()`:**
   - Instead of immediately copying static members, register them in `LazyStaticMemberRegistry`
   - Only create placeholder entries in `StructTypeInfo::static_members`

3. **Modify Static Member Access Handling:**
   - When accessing `ClassName::static_member`, check `LazyStaticMemberRegistry`
   - If lazy instantiation needed, perform it then
   - Update `StructTypeInfo` with the fully instantiated static member

**Benefits:**
- Reduces memory usage for template classes with many static constexpr members
- Particularly helps with `<type_traits>` which has thousands of `static constexpr bool value = ...`

**Estimated Complexity:** Medium
- Requires changes to ~3 locations in Parser.cpp
- New registry class similar to existing `LazyMemberInstantiationRegistry`

### Phase 2: Whole Template Class Lazy Instantiation

**Goal:** Defer complete instantiation of template classes until members are actually used.

**Current Behavior:**
- When `std::vector<int>` is referenced, the entire class structure is instantiated
- All base classes are resolved
- All members are processed
- Type information is fully computed

**Proposed Changes:**

1. **Create `LazyClassInstantiationRegistry`**:
```cpp
struct LazyClassInstantiationInfo {
    StringHandle template_name;           // e.g., "vector"
    StringHandle instantiated_name;       // e.g., "vector_int"
    std::vector<TemplateTypeArg> template_args;
    ASTNode template_declaration;         // Reference to primary template
    bool size_computed;                   // Has size/alignment been computed?
    bool members_instantiated;            // Have members been processed?
};

class LazyClassInstantiationRegistry {
    // Track partially instantiated classes
};
```

2. **Three-Phase Class Instantiation:**
   - **Phase A (Minimal):** Create type entry, register name
   - **Phase B (Layout):** Compute size/alignment (needed for sizeof, alignof, allocations)
   - **Phase C (Full):** Instantiate all members, base classes

3. **Trigger Points:**
   - Phase A: Any use of the type name
   - Phase B: `sizeof(T)`, `alignof(T)`, variable declarations, array allocations
   - Phase C: Member access, method calls, inheritance

**Benefits:**
- Significant reduction in instantiated types for complex template hierarchies
- `<type_traits>` often uses many intermediate types that are never fully used

**Estimated Complexity:** High
- Requires careful tracking of instantiation state
- Must handle circular dependencies
- Needs changes throughout type resolution code

### Phase 3: Type Alias Lazy Evaluation

**Goal:** Defer evaluation of template type aliases until actually needed.

**Current Behavior:**
```cpp
template<typename T>
struct remove_const { using type = T; };

template<typename T>
struct remove_const<const T> { using type = T; };

// Currently, when remove_const<const int> is used, 
// 'type' alias is evaluated immediately
```

**Proposed Changes:**

1. **Deferred Alias Resolution:**
   - Store alias target as unevaluated template expression
   - Only evaluate when the alias is actually accessed

2. **Alias Caching:**
   - Cache evaluated aliases to avoid re-computation

**Benefits:**
- Reduces work for unused type computations
- Many traits are computed but only `::value` or `::type` is accessed

**Estimated Complexity:** Medium-High
- Type alias evaluation is deeply integrated into type resolution

### Phase 4: Nested Type Lazy Instantiation

**Goal:** Defer instantiation of nested types until accessed.

**Example:**
```cpp
template<typename T>
struct outer {
    struct inner { T value; };  // Don't instantiate until used
};
```

**Proposed Changes:**
- Similar to whole template class lazy instantiation
- Track nested type instantiation separately from parent

**Estimated Complexity:** Medium

## Implementation Priority

| Phase | Feature | Benefit | Complexity | Priority |
|-------|---------|---------|------------|----------|
| 1 | Static Member Lazy | High | Medium | **HIGH** |
| 2 | Whole Class Lazy | Very High | High | **MEDIUM** |
| 3 | Type Alias Lazy | Medium | Medium-High | **LOW** |
| 4 | Nested Type Lazy | Medium | Medium | **LOW** |

## Testing Strategy

1. **Unit Tests:**
   - Add tests for each lazy instantiation component
   - Verify ODR-use triggers instantiation correctly
   - Test that unused components remain uninstantiated

2. **Integration Tests:**
   - Compile `<type_traits>` with lazy instantiation metrics
   - Compare memory usage and compilation time

3. **Correctness Tests:**
   - Ensure all existing tests pass
   - Verify semantic equivalence with eager instantiation

## Metrics to Track

1. **Number of template instantiations** (before/after)
2. **Peak memory usage** during compilation
3. **Compilation time** for standard headers
4. **Number of lazily deferred vs actually instantiated** components

## Related Issues

### Log Level Bug (Blocking Investigation)

During investigation, a critical bug was discovered:
- Setting global log level to Info (2) or below causes compilation to hang
- Memory grows exponentially (doubling every ~0.1s)
- Workaround: Use category-specific log levels (e.g., `--log-level=Parser:info`)
- This bug should be fixed before implementing new lazy instantiation features

**Root cause narrowed down (2026-01-18):**
- Issue is specific to global `LogConfig::runtimeLevel` setting
- Category-specific levels via `categoryLevels` map work correctly
- **The hang occurs during parsing of `__swappable_with_details` namespace in `<type_traits>`**
- Specifically related to parsing struct templates with complex SFINAE patterns:
  ```cpp
  template<typename _Tp, typename _Up, typename
           = decltype(swap(std::declval<_Tp>(), std::declval<_Up>()))>
  ```
- The issue is likely in `parse_struct_declaration()` or `parse_template_declaration()`
- When Debug-level logging is enabled, some code path behaves differently (possibly due to timing or side effects of log string construction)

## References

- `src/TemplateRegistry.h` - Template registry and lazy member infrastructure
- `src/InstantiationQueue.h` - Instantiation tracking
- `src/Parser.cpp:36196` - Current lazy member function implementation
- `src/CompileContext.h:266` - Lazy instantiation control flag
