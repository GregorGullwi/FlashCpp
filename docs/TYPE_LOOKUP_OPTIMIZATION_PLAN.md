# Type Lookup Optimization Plan

## Current Infrastructure

FlashCpp already has two mechanisms for type storage:

1. **`gTypeInfo`**: A `std::deque<TypeInfo>` where `TypeIndex` is simply an index into this deque
2. **`gTypesByName`**: A `std::unordered_map<StringHandle, const TypeInfo*>` for name-based lookups

The `TypeIndex` is already defined as `size_t` and used throughout the codebase (in `TypeSpecifierNode`, `StructMember`, `BaseClassSpecifier`, etc.).

### Current Lookup Patterns

```cpp
// Index-based lookup (already fast - O(1)):
const TypeInfo& type_info = gTypeInfo[type_index];

// Name-based lookup (requires hashing):
auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
```

## Problem: Template Instantiation Keys

The main performance concern is in **template instantiation caching**, where string keys are built:

```cpp
// Current approach - builds string keys for template cache
StringBuilder().append(template_name).append("_").append(type_name).commit();
```

## Template Arguments: Types vs Non-Type Values

Template arguments come in two forms that need different handling:

### Type Template Arguments
- Represent types like `int`, `std::string`, `MyClass`
- Can be represented by a `TypeIndex` (index into `gTypeInfo`)
- Example: `std::vector<int>` - the `int` is a type argument

### Non-Type Template Arguments  
- Represent compile-time constant values like integers, pointers, or enums
- Cannot be represented by `TypeIndex` - need the actual value
- Example: `std::array<int, 5>` - the `5` is a non-type argument
- Already stored as `std::vector<int64_t> non_type_template_args_` in the codebase

### Combined Template Key Structure

Most templates have 1-4 arguments, so we use an inline array to avoid heap allocation in the common case:

```cpp
// SmallVector-style container: inline storage for N elements, heap for more
template<typename T, size_t N = 4>
struct InlineVector {
    std::array<T, N> inline_data;
    std::vector<T> overflow;
    uint8_t inline_count = 0;
    
    void push_back(T value) {
        if (inline_count < N) {
            inline_data[inline_count++] = value;
        } else {
            overflow.push_back(value);
        }
    }
    
    size_t size() const { return inline_count + overflow.size(); }
    
    T operator[](size_t i) const {
        return i < N ? inline_data[i] : overflow[i - N];
    }
    
    bool operator==(const InlineVector& other) const {
        if (size() != other.size()) return false;
        for (size_t i = 0; i < size(); ++i) {
            if ((*this)[i] != other[i]) return false;
        }
        return true;
    }
};

struct TemplateInstantiationKey {
    TypeIndex base_template;                    // The template being instantiated
    InlineVector<TypeIndex, 4> type_args;       // Type arguments - inline up to 4
    InlineVector<int64_t, 4> non_type_args;     // Non-type arguments - inline up to 4
    
    bool operator==(const TemplateInstantiationKey&) const = default;
};

struct TemplateInstantiationKeyHash {
    size_t operator()(const TemplateInstantiationKey& key) const {
        size_t h = std::hash<TypeIndex>{}(key.base_template);
        for (size_t i = 0; i < key.type_args.size(); ++i) {
            h ^= std::hash<TypeIndex>{}(key.type_args[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (size_t i = 0; i < key.non_type_args.size(); ++i) {
            h ^= std::hash<int64_t>{}(key.non_type_args[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
```

This approach:
- **No heap allocation** for templates with ≤4 type args and ≤4 non-type args (covers ~95% of templates)
- **Falls back to vector** for larger template parameter lists
- **Still O(1) access** regardless of which storage is used

## Proposed Optimizations

### 1. Template Instantiation Cache by TypeIndex

Replace string-based template cache keys with numeric keys:

```cpp
// Current approach (string-based):
StringBuilder sb;
sb.append(template_name).append("_");
for (const auto& arg : template_args) {
    sb.append(getTypeName(arg)).append("_");
}
auto key = sb.commit();

// Proposed approach (TypeIndex + value based):
// Use TemplateInstantiationKey struct defined above
std::unordered_map<TemplateInstantiationKey, TypeIndex, TemplateInstantiationKeyHash> 
    gTemplateInstantiations;
```

### 2. Consistent TypeIndex Usage in AST Nodes

`TypeSpecifierNode` already stores `type_index_` - ensure it's used consistently:

```cpp
// Direct lookup via existing gTypeInfo:
const TypeInfo& getTypeInfo() const {
    return gTypeInfo[type_index_];
}
```

### 3. Function Overload Resolution by TypeIndex

Instead of comparing parameter type names:

```cpp
// Current approach:
bool matchesSignature(const std::vector<TypeSpecifierNode>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].type_name() != expected_params[i].type_name()) return false;
    }
    return true;
}

// Proposed approach:
bool matchesSignature(const std::vector<TypeIndex>& param_types) {
    return param_types == expected_param_types_;  // Direct vector comparison
}
```

## Implementation Phases

### Phase 1: Template Instantiation Cache
- [ ] Implement `TemplateInstantiationKey` with TypeIndex + non-type value based hashing
- [ ] Replace string-based template cache keys with numeric keys
- [ ] Update template instantiation code to use new cache

### Phase 2: Audit TypeIndex Usage
- [ ] Audit all TypeSpecifierNode usages
- [ ] Replace name-based lookups with index-based where TypeIndex is already available
- [ ] Ensure `gTypeInfo[type_index]` is used instead of `gTypesByName.find()`

### Phase 3: Function Resolution
- [ ] Store function signatures as `std::vector<TypeIndex>`
- [ ] Update overload resolution to compare TypeIndex vectors
- [ ] Cache function lookup results by signature hash

## Expected Benefits

1. **Performance**: Direct `gTypeInfo[type_index]` access is already O(1); the main gain is eliminating string concatenation for template instantiation keys
2. **Memory**: Numeric keys (TypeIndex + int64_t values) are more compact than string keys
3. **Type Safety**: TypeIndex comparisons can't have typos
4. **Simplicity**: No string building/concatenation for template cache lookups

## Existing Infrastructure (Already Available)

- `gTypeInfo`: `std::deque<TypeInfo>` - direct index-based access
- `gTypesByName`: `std::unordered_map<StringHandle, const TypeInfo*>` - name-based lookup  
- `TypeIndex`: `size_t` alias - used throughout AST nodes
- `TypeSpecifierNode::type_index_`: Already stores type index
- `non_type_template_args_`: Already stores non-type template arguments as `std::vector<int64_t>`

## Migration Strategy

1. Add `TemplateInstantiationKey`-based cache alongside existing string cache
2. Gradually migrate template instantiation lookups to new cache
3. Profile to confirm performance improvement before removing string cache

## Considerations

- **TypeIndex stability**: TypeIndex values are assigned during parsing. Consider if they need to be stable across compilation units (for incremental compilation).
- **Type aliases**: `typedef` and `using` create aliases that should resolve to a same TypeIndex
- **Template parameters**: Template type parameters need special handling (they're not concrete types until instantiation)

---

## Edge Cases and Handling Strategies

### Critical Edge Cases

#### 1. Template Template Parameters (template<typename> class Container)
**Problem**: Arguments that are themselves templates (e.g., `template<typename T> class Container`)
- Cannot be represented by TypeIndex alone (no concrete type yet)
- Need to track template name and nested parameters
- Example: `template<typename> class Op, std::array<int, Op>`

**Current handling**:
```cpp
// TemplateTypeArg already has:
bool is_template_template_arg;     // true if this is a template template argument
StringHandle template_name_handle;  // name of the template
```

**Proposed handling**:
- Add `template_template_name` field to TemplateInstantiationKey
- Include template's parameter list in key for disambiguation
- Store `StringHandle` of template name in key

**Updated key structure**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;                    // Base template name
    InlineVector<TypeIndex, 4> type_args;       // Type arguments
    InlineVector<int64_t, 4> non_type_args;     // Non-type arguments
    // NEW: Template template parameters
    std::vector<StringHandle> template_template_args;   // Names of template template params
    std::vector<std::vector<TypeIndex>> template_template_param_lists;  // Their parameters
};
```

#### 2. Dependent Types (T::iterator, T::value_type)
**Problem**: Types that depend on uninstantiated template parameters
- TypeIndex doesn't exist yet for `T::iterator` when T is a template parameter
- Need to preserve dependency information through instantiation
- Example: `template<typename T> using iterator = typename T::iterator;`

**Current handling**:
```cpp
// TemplateTypeArg already supports:
bool is_dependent;      // true if this type depends on uninstantiated template parameters
StringHandle dependent_name;  // name of the dependent template parameter or type name
```

**Proposed handling**:
- Don't create TypeIndex for dependent types
- Keep dependent type as unresolved until instantiation
- During instantiation, substitute dependent names with concrete types
- TemplateInstantiationKey should have separate handling for dependent vs resolved

**Implementation strategy**:
```cpp
// During template definition:
TemplateTypeArg arg;
arg.is_dependent = true;
arg.dependent_name = StringTable::getOrInternStringHandle("T::iterator");

// During instantiation with T=int:
// 1. Substitute: arg.dependent_name = "int::iterator"
// 2. Lookup type_index for "int::iterator"
// 3. Store in type_args vector as TypeIndex
```

#### 3. Partial Specializations with Dependent Patterns (template<typename T> struct Foo<T*>)
**Problem**: Template arguments themselves are type patterns, not concrete types
- `T*` where T is a template parameter is a dependent pattern
- TypeIndex for parameter, but pointer applies to parameter
- Example: `template<typename T> struct vector<T*>` - T is parameter, `T*` is dependent

**Current handling**:
```cpp
// TemplateTypeArg tracks pointer_depth and is_dependent
size_t pointer_depth;  // 0 = not pointer, 1 = T*, 2 = T**, etc.
bool is_dependent;     // true if depends on uninstantiated parameters
```

**Proposed handling**:
- For partial specializations, store pointer/reference info separately
- In TemplateInstantiationKey, track which arguments are patterns vs concrete
- Only resolve dependent patterns during instantiation

**Updated key structure**:
```cpp
struct TemplateTypeArgInfo {
    TypeIndex type_index;           // For concrete types
    bool is_dependent_pattern;      // For patterns like T*, T&, const T&
    size_t pattern_pointer_depth;     // Pointer depth in pattern
    size_t pattern_array_rank;       // Array dimensions in pattern
    StringHandle dependent_base;     // For T::value_type
};
```

#### 4. Recursive Templates (Factorial<N>, Fibonacci<N>)
**Problem**: Templates that instantiate themselves
- Potential for infinite recursion: `Factorial<N> = N * Factorial<N-1>`
- Need recursion depth detection and termination
- Example: `template<int N> struct Factorial { static constexpr int value = N * Factorial<N-1>::value; };`

**Current handling**:
- Parser tracks `current_template_param_names_` for detecting recursive patterns
- Template instantiation doesn't explicitly detect cycles

**Proposed handling**:
```cpp
// Add recursion tracking to TemplateRegistry:
struct RecursionGuard {
    std::unordered_set<template_instantiation_key> in_progress_;
    
    bool enter(const TemplateInstantiationKey& key) {
        if (in_progress_.contains(key)) {
            return false;  // Already instantiating - cycle detected!
        }
        in_progress_.insert(key);
        return true;
    }
    
    void exit(const TemplateInstantiationKey& key) {
        in_progress_.erase(key);
    }
};

// Usage in template instantiation:
RecursionGuard guard;
if (!guard.enter(key)) {
    throw std::runtime_error("Recursive template instantiation detected");
}
// ... instantiate template ...
guard.exit(key);
```

**Special cases**:
- Base case templates (e.g., `Factorial<0>`) - pre-instantiate these
- Maximum recursion depth (e.g., 1000) - prevent stack overflow
- Caching of base cases to avoid repeated recursion

#### 5. Variadic Templates with Unlimited Arguments (template<typename... Args>)
**Problem**: Parameter packs can have unlimited arguments
- InlineVector<N> won't work for unlimited
- Need dynamic storage or special handling
- Example: `template<typename... Args> struct Tuple;`

**Current handling**:
```cpp
// TemplateTypeArg has:
bool is_pack;  // true if this represents a parameter pack (typename... Args)
```

**Proposed handling**:
- Use std::vector for unlimited parameter packs
- Keep InlineVector for small optimization when pack is small (<=4 args)
- Track pack expansion count for hashing

**Updated key structure**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;
    InlineVector<TypeIndex, 4> type_args;       // For fixed args
    InlineVector<int64_t, 4> non_type_args;     // For fixed args
    // NEW: For variadic args
    std::vector<TypeIndex> variadic_type_args;       // Unlimited pack
    std::vector<int64_t> variadic_non_type_args; // Unlimited pack
    size_t variadic_arg_count;                    // Number of pack args
};
```

**Hash function update**:
```cpp
size_t operator()(const TemplateInstantiationKey& key) const {
    // Hash fixed args as before
    size_t h = std::hash<StringHandle>{}(key.template_name);
    // ... hash inline vectors ...
    
    // Add variadic args to hash
    for (const auto& type_arg : key.variadic_type_args) {
        h ^= std::hash<TypeIndex>{}(type_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    for (const auto& value_arg : key.variadic_non_type_args) {
        h ^= std::hash<int64_t>{}(value_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    h ^= std::hash<size_t>{}(key.variadic_arg_count);
    return h;
}
```

#### 6. Multiple Levels of Template Specialization (template<> template<> template<typename T>)
**Problem**: Nested template specializations require multiple keys
- `template<> template<> template<typename T> struct Foo<T*>::type`
- Each level needs its own instantiation key
- Dependencies between levels

**Current handling**:
- Template nesting tracked by `parsing_template_class_` stack
- Each level has its own template parameter context

**Proposed handling**:
- Each template level gets its own TemplateInstantiationKey
- Parent keys stored as dependencies
- Cache should include hierarchy information

**Updated key structure**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;
    InlineVector<TypeIndex, 4> type_args;
    InlineVector<int64_t, 4> non_type_args;
    // NEW: Template nesting information
    TemplateInstantiationKey* parent_key;  // Parent instantiation (for nested templates)
    int nesting_level;                      // 0 = top-level, 1 = one level nested, etc.
};
```

#### 7. Type Aliases in Template Arguments (std::vector<T> where vector is alias)
**Problem**: Template arguments through type aliases
- Alias resolution chains: `template<typename T> using Vec = std::vector<T>` then `Vec<int>`
- Need to resolve alias to underlying template
- Example: `template<typename T> using Ptr = T*; template<typename T> struct Container { using type = Ptr<T>; };`

**Current handling**:
- `get_instantiated_class_name()` builds string names including aliases
- TemplateTypeArg stores `is_template_template_arg` but not alias chain

**Proposed handling**:
- Track alias chains separately from direct instantiations
- Alias resolution should create new TemplateInstantiationKey pointing to underlying template
- Store alias resolution result in cache

**Implementation**:
```cpp
struct AliasResolution {
    StringHandle alias_name;
    std::vector<StringHandle> alias_params;  // Parameters of the alias
    TemplateInstantiationKey resolved_key;      // The key this alias resolves to
};

// In TemplateInstantiationKey:
struct TemplateInstantiationKey {
    StringHandle template_name;
    // ... other fields ...
    // NEW: Alias tracking
    std::optional<AliasResolution> alias_resolution;
    bool is_alias_instantiation;  // True if this came from an alias
};
```

#### 8. Template Arguments with CV Qualifiers (const T, volatile T, const volatile T)
**Problem**: CV qualifiers on template arguments
- `template<typename T> struct Foo { void bar(const T&); }`
- TypeIndex should capture CV qualifiers
- CV qualifiers affect type matching

**Current handling**:
```cpp
// TemplateTypeArg has:
CVQualifier cv_qualifier;  // const/volatile qualifiers
```

**Proposed handling**:
- CV qualifiers already supported in TemplateTypeArg
- Ensure they're included in TemplateInstantiationKey comparison
- Hash function should include cv_qualifier

**Hash function update**:
```cpp
// Already includes cv_qualifier:
hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(arg.cv_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

// Ensure TemplateInstantiationKey operator== compares CV qualifiers:
return base_type == other.base_type &&
       type_index_match &&
       cv_qualifier == other.cv_qualifier &&  // Should already be there
       // ... other comparisons ...
```

#### 9. Template Arguments with Pointer/Reference Levels (T**, T&&, T&*)
**Problem**: Complex pointer/reference combinations
- `template<typename T> struct Foo { T*** ptr; T&& ref; };`
- Need to track multi-level pointers accurately
- References to pointers, pointers to references

**Current handling**:
```cpp
// TemplateTypeArg has:
size_t pointer_depth;         // 0 = not pointer, 1 = T*, 2 = T**, etc.
bool is_reference;           // true if this is a reference
bool is_rvalue_reference;    // true if this is an rvalue reference (&&)
```

**Proposed handling**:
- Current fields already handle these cases correctly
- Ensure TemplateInstantiationKey includes these in comparison
- Special case: pointers to member functions `T::*`

**Test cases**:
- `T*` - simple pointer (pointer_depth=1)
- `T**` - double pointer (pointer_depth=2)
- `T&&` - rvalue reference (is_rvalue_reference=true)
- `T&` - lvalue reference (is_reference=true)
- `T**&` - reference to pointer (pointer_depth=2, is_reference=true)
- `T::*` - pointer to member (member_pointer_kind != None)

#### 10. Non-Type Template Arguments as Expressions (template<int N = 42+5>)
**Problem**: Compile-time constant expressions as template arguments
- `template<int N = sizeof(T)> struct Foo;`
- `template<int N = alignof(T)> struct Bar;`
- Need to evaluate expression to constant value

**Current handling**:
```cpp
// TemplateTypeArg has:
int64_t value;        // For non-type template parameters
Type value_type;       // For non-type arguments: type of the value (bool, int, etc.)
```

**Proposed handling**:
- Store evaluated constant expression in TemplateInstantiationKey
- Don't store expression AST node (only store evaluated value)
- Evaluate during template definition, not during instantiation

**Implementation**:
```cpp
struct NonTypeConstant {
    int64_t value;          // Evaluated constant value
    Type type;               // Type of the expression (int, bool, etc.)
    bool is_evaluated;      // True if successfully evaluated
};

// In TemplateInstantiationKey:
struct TemplateInstantiationKey {
    // ... other fields ...
    // NEW: For complex non-type args
    std::vector<NonTypeConstant> evaluated_non_type_args;
    std::vector<ASTNode> unevaluated_expressions;  // Fallback if evaluation failed
};
```

#### 11. Function Type Template Arguments (template<typename R, typename... Args> R(*)(Args...))
**Problem**: Template arguments representing function types
- Function signatures as template parameters
- Complex representation: return type + parameter types
- Example: `template<typename T> struct FunctionPtr { using type = void(*)(T*); };`

**Current handling**:
- Function types parsed as TypeSpecifierNode with `type() == Type::Function`
- TypeIndex assigned to function types

**Proposed handling**:
- Function types should have their own TypeIndex range
- Store function signature in TypeInfo
- TemplateTypeArg can reference function TypeIndex

**Implementation**:
```cpp
struct FunctionTypeInfo {
    TypeIndex return_type;
    std::vector<TypeIndex> parameter_types;
    CallingConvention calling_convention;
    bool is_variadic;
};

// In TypeInfo (gTypeInfo):
struct TypeInfo {
    // ... existing fields ...
    std::optional<FunctionTypeInfo> function_info;  // NEW: For function types
};
```

#### 12. Array Type Template Arguments (T[N], T[M][N])
**Problem**: Array types with compile-time dimensions
- `template<typename T, int N> struct Array { T data[N]; };`
- Multiple dimensions: `T[N][M]`
- Need to track dimensions and sizes

**Current handling**:
```cpp
// TemplateTypeArg has:
bool is_array;                    // true if this is an array
std::optional<size_t> array_size;  // Known array size if available
```

**Proposed handling**:
- Current structure supports single-dimensional arrays
- For multi-dimensional, store vector of sizes
- Array types get unique TypeIndex per dimension signature

**Updated key structure**:
```cpp
struct TemplateTypeArg {
    // ... existing fields ...
    bool is_array;
    // NEW: For multi-dimensional arrays
    std::vector<size_t> array_dimensions;  // All dimensions: [N, M] for T[N][M]
};

// Alternative: Create TypeIndex per unique array signature
// e.g., Array_int_5_10 is different from Array_int_5
```

#### 13. Lambda Capture Types (decltype(lambda), closure types)
**Problem**: Lambda types as template arguments
- Lambda types generated by compiler, not user-defined
- Need to capture type in instantiation key
- Example: `template<typename F> void call(F f); call([](int x) { return x; });`

**Current handling**:
- Lambda types assigned TypeIndex when created
- Lambda types are user-defined types in TypeInfo

**Proposed handling**:
- Lambda types should be treated like any other user-defined type
- No special handling needed if TypeIndex is assigned correctly
- Ensure lambda closure types are stable across compilation

**Potential issue**:
- Lambda types in different scopes might get different TypeIndex
- Template instantiation with lambda might not find same TypeIndex
- Solution: Use mangled lambda name as stable key

#### 14. SFINAE and Failed Instantiations
**Problem**: Substitution failures should not create cache entries
- `template<typename T> struct SFINAE<T, typename T::iterator>` - fails if T has no iterator
- Failed instantiations shouldn't pollute cache
- But should cache that certain arguments cause failure

**Current handling**:
- SFINAE context tracked by `in_sfinae_context_`
- Failed instantiations don't cache (I think?)

**Proposed handling**:
```cpp
struct TemplateInstantiationResult {
    bool success;
    std::optional<TypeIndex> type_index;  // On success
    std::string error_message;              // On failure
};

// Separate cache for failed instantiations:
std::unordered_map<TemplateInstantiationKey, std::string> gFailedInstantiations;

// In TemplateRegistry:
std::optional<ASTNode> tryGetInstantiation(const TemplateInstantiationKey& key) const {
    // Check cache first
    if (auto it = instantiations_.find(key); it != instantiations_.end()) {
        return it->second;
    }
    // Check failed instantiations
    if (auto it = gFailedInstantiations.find(key); it != gFailedInstantiations.end()) {
        // Return failure marker without re-instantiating
        return std::nullopt;  // or special error node
    }
    // Try instantiation
    // On failure: gFailedInstantiations[key] = error_message;
    // On success: instantiations_[key] = result;
}
```

#### 15. Template Instantiation in Different Scopes (namespace::Template vs class member)
**Problem**: Same template name in different scopes
- `struct Foo { template<typename T> struct Bar; };`
- `namespace Ns { template<typename T> struct Bar; };`
- Different instantiations, should be separate cache entries

**Current handling**:
- Namespaces tracked by NamespaceRegistry
- Class members in StructTypeInfo

**Proposed handling**:
- Include scope/namespace in TemplateInstantiationKey
- Differentiate instantiations by scope context

**Updated key structure**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;
    InlineVector<TypeIndex, 4> type_args;
    InlineVector<int64_t, 4> non_type_args;
    // NEW: Scope information
    std::vector<StringHandle> namespace_qualifiers;  // e.g., ["std", "vector"] for std::vector
    TypeIndex enclosing_class;                  // For class member templates
    StringHandle scope_unique_key;               // Combines namespace + class for uniqueness
};
```

#### 16. Out-of-Line Template Member Definitions (template<typename T> void Foo<T>::method())
**Problem**: Template members defined outside class
- Instantiation happens with template arguments
- But definition location is outside class
- Need to track both declaration and definition locations

**Current handling**:
- OutOfLineMemberFunction struct tracks body position
- Template member functions registered in TemplateRegistry

**Proposed handling**:
- TemplateInstantiationKey should work for out-of-line definitions
- No special handling needed if TypeIndex is used consistently
- Ensure member function gets correct instantiation

#### 17. Template Instantiation with constexpr Evaluation
**Problem**: Constant evaluation during instantiation
- `template<int N> struct Array { static constexpr int size = N * 2; };`
- Template argument might be constexpr expression, not just literal
- Need to evaluate expressions before creating key

**Current handling**:
- ConstExprEvaluator handles constant expressions
- Template instantiation calls evaluator

**Proposed handling**:
- Ensure TemplateInstantiationKey stores evaluated values, not expressions
- All non-type template args must be reduced to constants before caching
- Evaluation errors should propagate to template instantiation failure

**Implementation**:
```cpp
// During template instantiation:
for (size_t i = 0; i < template_args.size(); ++i) {
    if (template_args[i].is_value && !template_args[i].is_evaluated) {
        auto result = ConstExprEvaluator::evaluate(template_args[i].value_expression);
        if (!result.has_value()) {
            return TemplateInstantiationResult::failure(result.error());
        }
        template_args[i].value = result.value();
        template_args[i].is_evaluated = true;
    }
}

// Now create TemplateInstantiationKey with evaluated values
```

#### 18. Template Instantiation with Concepts (template<typename T> requires Integral<T>)
**Problem**: Requires clauses affect instantiation
- Templates can't be instantiated if concept doesn't satisfy
- Requires clause needs to be part of instantiation key
- Example: `template<typename T> requires std::is_integral_v<T> struct Foo;`

**Current handling**:
- Concepts tracked in ConceptRegistry
- Requires clauses stored in template nodes

**Proposed handling**:
- Include satisfied concepts in TemplateInstantiationKey
- Failed concept satisfaction shouldn't create cache entry
- Concept evaluation happens before instantiation

**Updated key structure**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;
    InlineVector<TypeIndex, 4> type_args;
    InlineVector<int64_t, 4> non_type_args;
    // NEW: Concept satisfaction
    std::vector<StringHandle> satisfied_concepts;  // All concepts that must be satisfied
    bool concepts_evaluated;                // True if concepts checked
};

// Instantiation flow:
// 1. Evaluate concept satisfaction
// 2. If fails: cache as failed instantiation (see edge case #14)
// 3. If succeeds: proceed with instantiation
```

#### 19. Default Template Arguments Not Provided (template<typename T = int> struct Foo)
**Problem**: Default arguments vs explicit arguments
- Template instantiation key shouldn't treat default args as explicit
- Need to distinguish `Foo<int>` vs `Foo<>` (using default)
- Example: `template<typename T = int> struct Foo; Foo<> x;` uses default int

**Current handling**:
- Default arguments stored in template parameter nodes
- When args not provided, defaults are used

**Proposed handling**:
- TemplateInstantiationKey needs flag for each arg: was default provided or explicit
- Or: Default args filled in before creating key
- Two separate cache entries for `Foo<int>` vs `Foo<>`

**Implementation**:
```cpp
struct TemplateInstantiationKey {
    StringHandle template_name;
    InlineVector<TypeIndex, 4> type_args;
    InlineVector<int64_t, 4> non_type_args;
    // NEW: Track which args are defaults
    std::bitset<8> args_are_defaults;  // Bit N = true if arg N was defaulted
};

// During key creation:
for (size_t i = 0; i < template_params.size(); ++i) {
    if (i < provided_args.size()) {
        // Explicit argument
        key.type_args[i] = provided_args[i];
        key.args_are_defaults[i] = false;
    } else {
        // Use default argument
        key.type_args[i] = template_params[i].default_type;
        key.args_are_defaults[i] = true;
    }
}
```

#### 20. Template Instantiation Overload Resolution
**Problem**: Multiple template specializations can match
- Need to select most specialized (partial ordering rules)
- `template<typename T> struct Foo<T*>` vs `template<typename T> struct Foo<T>`
- Should pick `Foo<int*>` not `Foo<int>` (more specialized)

**Current handling**:
- Template specialization matching exists
- Uses partial specialization rules

**Proposed handling**:
- When multiple TemplateInstantiationKeys match arguments:
  1. Try most specialized first (with patterns)
  2. Fall back to less specialized
  3. Cache separate entries for each overload
- Instantiation key should include specialization depth

**Implementation**:
```cpp
struct TemplateInstantiationKey {
    // ... existing fields ...
    // NEW: Specialization ranking
    int specialization_rank;  // 0 = primary, 1 = more specialized, etc.
    bool is_partial_specialization;  // True if this is a partial spec
};

// Overload resolution:
std::vector<ASTNode> findInstantiation(const std::string& name, 
                                       const std::vector<TemplateTypeArg>& args) {
    std::vector<TemplateInstantiationKey> candidates;
    
    // Find all matching instantiations
    for (const auto& [key, node] : gTemplateRegistry.instantiations_) {
        if (key.template_name == name) {
            if (matchesArgs(key, args)) {
                candidates.push_back({key, node});
            }
        }
    }
    }
    
    // Sort by specialization_rank (higher = more specialized)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.key.specialization_rank > b.key.specialization_rank;
              });
    
    // Return most specialized
    return candidates.empty() ? std::vector<>() 
                           : std::vector<>{candidates[0].node};
}
```

---

## Implementation Checklist

### Phase 1: Data Structures
- [ ] Update `TemplateInstantiationKey` with edge case fields
  - [ ] Add `template_template_args` vector
  - [ ] Add `variadic_type_args` and `variadic_non_type_args` for unlimited packs
  - [ ] Add `namespace_qualifiers` for scope disambiguation
  - [ ] Add `specialization_rank` for overload resolution
  - [ ] Add `parent_key` for nested templates
  - [ ] Add `args_are_defaults` bitset for default arg tracking

### Phase 2: Hash Function Updates
- [ ] Update `TemplateInstantiationKeyHash::operator()` for new fields
  - [ ] Hash variadic vectors
  - [ ] Hash namespace qualifiers
  - [ ] Hash specialization_rank
  - [ ] Hash parent_key (recursive hash for nested templates)

### Phase 3: Cache Lookups
- [ ] Update `try_instantiate_template()` to use new key structure
- [ ] Handle dependent types during template definition (don't create TypeIndex yet)
- [ ] Implement variadic pack detection and vector usage
- [ ] Add concept evaluation before instantiation
- [ ] Add SFINAE failure caching

### Phase 4: Special Case Handlers
- [ ] Implement template template parameter handling
- [ ] Implement recursive template detection and cycle prevention
- [ ] Implement type alias resolution tracking
- [ ] Implement constexpr expression evaluation for non-type args
- [ ] Implement overload resolution with specialization ranking
- [ ] Implement default argument handling

### Phase 5: Testing
- [ ] Add tests for all edge cases
  - [ ] Template template parameters
  - [ ] Dependent types
  - [ ] Recursive templates
  - [ ] Variadic templates
  - [ ] Multi-level specializations
  - [ ] Type aliases
  - [ ] CV-qualified args
  - [ ] Complex pointer/reference combinations
  - [ ] Non-type expression args
  - [ ] Function type args
  - [ ] Multi-dimensional arrays
  - [ ] Lambda types
  - [ ] SFINAE failures
  - [ ] Different scopes
  - [ ] Out-of-line definitions
  - [ ] Concepts
  - [ ] Default arguments

### Phase 6: Performance Validation
- [ ] Benchmark instantiation before changes
- [ ] Benchmark instantiation after changes
- [ ] Compare cache hit rates
- [ ] Compare memory usage
- [ ] Validate no regression in compilation speed

---

## Risk Mitigation

### Rollback Strategy
If the new implementation causes issues:
1. Keep old string-based cache as fallback
2. Compile-time flag to enable/disable new cache: `-DUSE_TYPEINDEX_CACHE=1`
3. Gradual rollout: Test with subset of templates first
4. Instrumentation: Add logging to track cache hits/misses

### Validation Steps
Before removing old string-based cache:
1. Run full test suite (600+ tests)
2. Compile real-world code with complex templates
3. Validate cache hit rate > 90% for common patterns
4. Check memory usage doesn't increase significantly
5. Verify no duplicate instantiations in cache
6. Confirm SFINAE failures still work correctly

### Known Limitations of Proposed Approach
1. TypeIndex may not be stable across compilation units (for incremental compilation)
   - Mitigation: Use stable hashing based on type characteristics
2. Template template parameters may increase key size
   - Mitigation: Template names are interned strings, minimal overhead
3. Variadic templates with many args may use heap allocation
   - Mitigation: 95% of templates have <=4 args, inline storage covers most cases
4. Dependent types can't use TypeIndex
   - Mitigation: Keep dependent type info separate from cache

