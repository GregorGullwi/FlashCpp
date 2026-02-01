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

### Immediate Fix (Phase 1A)

The current cache key stores only `Type` enums for arguments, which collapses
distinct struct types into the same key. The first step is to store full
`TemplateTypeArg` entries (including `TypeIndex`) for template instantiation
keys, so struct types like `Wrapper_int` and `Wrapper_double` no longer collide.

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
- Already stored as `std::vector<int64_t> value_arguments` (for non-type parameters) in `TemplateInstantiationKey`
- **Note**: Non-type template arguments are used by all template kinds (class templates like `std::array<int, 5>`, function templates, variable templates, and alias templates), not just function templates
- **Migration**: Current `std::vector` should be changed to `ChunkedVector<int64_t>` for better memory allocation patterns

### Combined Template Key Structure

Most templates have 1-4 arguments, so we use an inline array to avoid heap allocation in the common case:

```cpp
// SmallVector-style container: inline storage for N elements, heap for more
template<typename T, size_t N = 4>
struct InlineVector {
    std::array<T, N> inline_data;
    ChunkedVector<T> overflow;  // Use ChunkedVector for better memory allocation
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
    
    bool empty() const { return inline_count == 0 && overflow.empty(); }
    size_t capacity() const { return N + overflow.capacity(); }
    
    // Iterator support for range-based for loops
    // Note: We use index-based iteration since inline_data and overflow are separate regions
    class const_iterator {
        const InlineVector* vec;
        size_t index;
    public:
        const_iterator() : vec(nullptr), index(0) {}
        const_iterator(const InlineVector& v, size_t i) : vec(&v), index(i) {}
        const T& operator*() const { return (*vec)[index]; }
        const_iterator& operator++() { ++index; return *this; }
        bool operator!=(const const_iterator& other) const { 
            return index != other.index; 
        }
        bool operator==(const const_iterator& other) const { 
            return index == other.index; 
        }
    };
    
    using iterator = const_iterator;  // InlineVector is typically immutable after construction
    
    const_iterator begin() const { return const_iterator(*this, 0); }
    const_iterator end() const { return const_iterator(*this, size()); }
};

// ============================================================================
// Memory-Optimized Template Key Design
// ============================================================================
// 
// SIZE ANALYSIS:
// The monolithic struct would be ~400-500 bytes due to:
// - Core fields: ~100 bytes (used by 95% of templates)
// - 7x ChunkedVector empty overhead: ~168 bytes (each ~24 bytes)
// - Optional<AliasResolution>: ~40-80 bytes (includes another key!)
// - Various bools/padding: ~20-30 bytes
// Total: ~350-400 bytes per key, even for simple vector<int>!
//
// SOLUTION: Split into Base + Extension pattern
// - BaseKey: ~100 bytes for common case (vector<int>, array<T, N>, etc.)
// - Extension: Pointer to additional data only when needed
// - Estimated savings: 70-80% memory reduction for common templates

// ============================================================================
// BASE KEY (used for 95% of templates)
// ============================================================================

struct TemplateInstantiationBaseKey {
    // Core fields (always present, ~80-100 bytes total)
    StringHandle template_name;                    // Base template name
    TypeIndex base_template;                       // Template type index
    
    // Arguments (fixed inline storage covers ~95% of use cases)
    InlineVector<TypeIndex, 4> type_args;          // Type arguments
    InlineVector<int64_t, 4> value_arguments;      // Non-type arguments
    
    // Packed flags (bitmask to save space)
    uint32_t flags;  // Bit 0: has_extension, Bit 1: is_variadic, Bit 2: is_scoped, etc.
    
    // Quick scope check (often empty, but frequently needed)
    StringHandle scope_hash;  // 0 = global scope, otherwise combined namespace+class hash
    
    bool operator==(const TemplateInstantiationBaseKey&) const = default;
};

// ============================================================================
// EXTENSION DATA (only allocated when needed)
// ============================================================================

struct TemplateInstantiationExtension {
    // Variadic templates (only when >4 args)
    ChunkedVector<TypeIndex> variadic_type_args;
    ChunkedVector<int64_t> variadic_value_args;
    
    // Template template parameters (rare)
    ChunkedVector<StringHandle> template_template_args;
    
    // Detailed scope info (when scope_hash is non-zero)
    ChunkedVector<StringHandle> namespace_qualifiers;
    TypeIndex enclosing_class;
    
    // Template nesting (rare)
    TemplateInstantiationBaseKey* parent_key;
    int nesting_level;
    
    // Type aliases (rare)
    std::unique_ptr<AliasResolution> alias_resolution;
    
    // C++20 concepts (rare)
    ChunkedVector<StringHandle> satisfied_concepts;
    
    // Complex expressions (rare)
    ChunkedVector<NonTypeConstant> evaluated_value_args;
    
    // Specialization info (for overload resolution)
    int specialization_rank;
    uint16_t args_are_defaults;  // Bitmask for default args (up to 16 args)
};

// ============================================================================
// COMPLETE KEY (base + optional extension)
// ============================================================================

struct TemplateInstantiationKey {
    TemplateInstantiationBaseKey base;
    std::unique_ptr<TemplateInstantiationExtension> ext;  // nullptr for 95% of templates
    
    // Helper accessors (delegate to base or extension)
    StringHandle template_name() const { return base.template_name; }
    bool is_variadic() const { return base.flags & (1u << 1); }
    ChunkedVector<TypeIndex>& variadic_type_args() { 
        if (!ext) ext = std::make_unique<TemplateInstantiationExtension>();
        return ext->variadic_type_args; 
    }
    
    bool operator==(const TemplateInstantiationKey& other) const {
        if (!(base == other.base)) return false;
        if (!ext && !other.ext) return true;
        if (!ext || !other.ext) return false;
        // Compare extensions...
        return true;
    }
};

// Supporting structures (unchanged)
struct AliasResolution {
    StringHandle alias_name;
    ChunkedVector<StringHandle> alias_params;
    TemplateInstantiationKey resolved_key;
};

struct NonTypeConstant {
    int64_t value;
    Type type;
    bool is_evaluated;
};

struct TemplateInstantiationKeyHash {
    size_t operator()(const TemplateInstantiationKey& key) const {
        // Hash base fields (always present)
        size_t h = std::hash<TypeIndex>{}(key.base.base_template);
        h ^= std::hash<uint32_t>{}(key.base.flags);
        
        for (const auto& type_arg : key.base.type_args) {
            h ^= std::hash<TypeIndex>{}(type_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (const auto& value_arg : key.base.value_arguments) {
            h ^= std::hash<int64_t>{}(value_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        
        // Hash extension fields (only if present)
        if (key.ext) {
            for (const auto& type_arg : key.ext->variadic_type_args) {
                h ^= std::hash<TypeIndex>{}(type_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            for (const auto& value_arg : key.ext->variadic_value_args) {
                h ^= std::hash<int64_t>{}(value_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            h ^= std::hash<int>{}(key.ext->specialization_rank);
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
bool matchesSignature(const ChunkedVector<TypeIndex>& param_types) {
    return param_types == expected_param_types_;  // Direct ChunkedVector comparison
}
```

## Implementation Phases

### Phase 1: Template Instantiation Cache
- [ ] Update `TemplateInstantiationKey` to store `TemplateTypeArg` (TypeIndex + qualifiers) for cache keys
- [ ] Replace Type-only cache keys with TypeIndex + non-type value based keys
- [ ] Update template instantiation code to use the new cache key

### Phase 2: Audit TypeIndex Usage
- [ ] Audit all TypeSpecifierNode usages
- [ ] Replace name-based lookups with index-based where TypeIndex is already available
- [ ] Ensure `gTypeInfo[type_index]` is used instead of `gTypesByName.find()`

### Phase 2.5: Base Template Name Tracking
- [ ] Track base template name per instantiation to avoid parsing underscores
- [ ] Use base-name mapping when deducing template-template parameters
- [ ] Use base-name mapping when resolving member templates on instantiated classes

### Phase 3: Function Resolution
- [ ] Store function signatures as `ChunkedVector<TypeIndex>`
- [ ] Update overload resolution to compare TypeIndex vectors
- [ ] Cache function lookup results by signature hash

### Phase 4: Template Edge Cases
- [ ] Prefer base-template name mapping over underscore parsing in dependent member alias resolution

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
- `value_arguments`: Already stores non-type template arguments as `std::vector<int64_t>`
- **Migration Note**: These std::vectors should be changed to `ChunkedVector<T>` for better memory allocation patterns

## Migration Strategy

1. Add `TemplateInstantiationKey`-based cache alongside existing string cache
2. Gradually migrate template instantiation lookups to new cache
3. Profile to confirm performance improvement before removing string cache

## Considerations

- **TypeIndex stability**: TypeIndex values are assigned during parsing. Consider if they need to be stable across compilation units (for incremental compilation).
- **Type aliases**: `typedef` and `using` create aliases that should resolve to a same TypeIndex
- **Template parameters**: Template type parameters need special handling (they're not concrete types until instantiation)

---

## Memory Optimization Strategy

### Why Split the Key?

**Problem**: A monolithic struct with all fields would be ~400-500 bytes per key, even for simple templates like `vector<int>`.

**Solution**: Base + Extension pattern
- **BaseKey**: ~80-100 bytes, handles 95% of templates (vector<int>, array<T,N>, etc.)
- **Extension**: Allocated only when needed (variadic, scoped, nested, etc.)
- **Memory savings**: 70-80% reduction for common cases

### Field Allocation Strategy

| Data | Location | When Allocated |
|------|----------|----------------|
| template_name, base_template | Base | Always |
| type_args, value_arguments (≤4) | Base | Always |
| scope_hash (quick check) | Base | Always (0 if global scope) |
| flags bitmask | Base | Always |
| Variadic args (>4) | Extension | Only if >4 args |
| Template template params | Extension | Only if present |
| Namespace qualifiers | Extension | Only if non-global scope |
| Parent key, nesting level | Extension | Only for nested templates |
| Alias resolution | Extension | Only if alias |
| Concepts | Extension | Only if requires clause |
| Complex expressions | Extension | Only if non-trivial constexpr |
| Specialization rank | Extension | Only for partial specs |

## Edge Cases and Handling Strategies

Each edge case below indicates whether fields are in **Base** or **Extension**. The implementation lazily allocates the Extension only when edge case fields are needed.

### Critical Edge Cases

#### 1. Template Template Parameters (template<typename> class Container)
**Problem**: Arguments that are themselves templates (e.g., `template<typename T> class Container`)
- Cannot be represented by TypeIndex alone (no concrete type yet)
- Need to track template name and nested parameters
- Example: `template<typename> class Op, std::array<int, Op>`

**Related Fields**:
- `template_template_args` - Stores names of template template parameters (e.g., "Container", "Allocator")
- `template_template_param_lists` - Stores the parameter lists for each template template arg

**Current handling**:
```cpp
// TemplateTypeArg already has:
bool is_template_template_arg;     // true if this is a template template argument
StringHandle template_name_handle;  // name of the template
```

**Implementation**:
```cpp
TemplateInstantiationKey key;
key.template_name = StringTable::getOrInternStringHandle("my_template");

// For: template<template<typename> class Container>
//      struct MyClass { ... };
if (arg.is_template_template_arg) {
    key.template_template_args.push_back(arg.template_name_handle);
    // Also store the template's own parameters if needed
    ChunkedVector<TypeIndex> params;
    // ... populate params ...
    key.template_template_param_lists.push_back(std::move(params));
}
```

#### 2. Dependent Types (T::iterator, T::value_type)
**Problem**: Types that depend on uninstantiated template parameters
- TypeIndex doesn't exist yet for `T::iterator` when T is a template parameter
- Need to preserve dependency information through instantiation
- Example: `template<typename T> using iterator = typename T::iterator;`

**Related Fields**:
- `type_args` - After resolution, stores the concrete TypeIndex (e.g., int::iterator -> TypeIndex)
- Dependent types are resolved BEFORE creating the key, so the key always contains concrete TypeIndex values

**Current handling**:
```cpp
// TemplateTypeArg already supports:
bool is_dependent;      // true if this type depends on uninstantiated template parameters
StringHandle dependent_name;  // name of the dependent template parameter or type name
```

**Implementation Strategy**:
```cpp
// During template definition:
TemplateTypeArg arg;
arg.is_dependent = true;
arg.dependent_name = StringTable::getOrInternStringHandle("T::iterator");

// During instantiation with T=int:
// 1. Substitute: arg.dependent_name = "int::iterator"
// 2. Lookup type_index for "int::iterator"
// 3. Store in type_args vector as TypeIndex
TemplateInstantiationKey key;
key.type_args.push_back(resolved_type_index);  // Concrete type, not dependent
```

**Key Design Principle**: TemplateInstantiationKey ALWAYS contains resolved, concrete types. Dependent type resolution happens during instantiation BEFORE the key is created.

#### 3. Partial Specializations with Dependent Patterns (template<typename T> struct Foo<T*>)
**Problem**: Template arguments themselves are type patterns, not concrete types
- `T*` where T is a template parameter is a dependent pattern
- Pattern matching happens BEFORE creating the instantiation key
- Example: `template<typename T> struct vector<T*>` - T is parameter, `T*` is pattern

**Related Fields**:
- `type_args` - Stores the final resolved concrete types after pattern matching
- `specialization_rank` - Used to select the most specialized matching partial specialization
- `is_partial_specialization` - Marks if this is a partial specialization vs primary template

**Pattern Matching Phase** (BEFORE key creation):
```cpp
// TemplateTypeArg tracks pattern information during matching
struct TemplateTypeArg {
    TypeIndex type_index;           // Base type (e.g., T in T*)
    size_t pointer_depth;           // 0 = T, 1 = T*, 2 = T**, etc.
    bool is_dependent;              // true if depends on template parameters
    // ... other fields
};

// During pattern matching for vector<T*> with T=int:
// 1. Pattern arg: pointer_depth=1, base_type=T (Type::UserDefined)
// 2. Concrete arg: pointer_depth=1, base_type=int (Type::Int)
// 3. Match succeeds: T=int with pointer modifier
// 4. Key stores: type_args[0] = TypeIndex for int*
```

**Key Design**: Pattern matching uses TemplateTypeArg with modifiers. Once matched, the key stores only the resolved TypeIndex in `type_args`. The `specialization_rank` field helps select between multiple matching partial specializations.

#### 4. Recursive Templates (Factorial<N>, Fibonacci<N>)
**Problem**: Templates that instantiate themselves
- Potential for infinite recursion: `Factorial<N> = N * Factorial<N-1>`
- Need recursion depth detection and termination
- Example: `template<int N> struct Factorial { static constexpr int value = N * Factorial<N-1>::value; };`

**Related Fields**:
- The complete `TemplateInstantiationKey` itself is used for cycle detection in a `RecursionGuard`
- `value_arguments` stores N, which decreases in recursive instantiations (Factorial<5> -> Factorial<4> -> ...)

**Implementation**:
```cpp
struct RecursionGuard {
    std::unordered_set<TemplateInstantiationKey, TemplateInstantiationKeyHash> in_progress_;
    
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

// Usage:
TemplateInstantiationKey key;
key.template_name = "Factorial"_sh;
key.value_arguments.push_back(5);  // Factorial<5>

RecursionGuard guard;
if (!guard.enter(key)) {
    throw std::runtime_error("Recursive template instantiation detected");
}
// Key uniqueness ensures Factorial<5> != Factorial<4> != Factorial<3>...
// Recursion terminates naturally when reaching Factorial<0> (base case)
guard.exit(key);
```

**Special cases**:
- Base case templates (e.g., `Factorial<0>`) - pre-instantiate and cache
- Maximum recursion depth (e.g., 1000) - prevent stack overflow
- The key naturally prevents false cycles because Factorial<5> and Factorial<4> have different `value_arguments`

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
- Use ChunkedVector for unlimited parameter packs (better memory allocation than std::vector)
- Keep InlineVector for small optimization when pack is small (<=4 args)
- Track pack expansion count for hashing

**Related Fields**:
- `variadic_type_args` - Stores type packs (e.g., int, float, double in tuple<int, float, double>)
- `variadic_value_args` - Stores non-type packs (e.g., 1, 2, 3 in int_sequence<1, 2, 3>)
- `variadic_arg_count` - Total count for hash function

**Implementation**:
```cpp
// Example: tuple<int, float, double>
TemplateInstantiationKey key;
key.template_name = "tuple"_sh;
key.variadic_type_args.push_back(int_type_index);
key.variadic_type_args.push_back(float_type_index);
key.variadic_type_args.push_back(double_type_index);
key.variadic_arg_count = 3;

// Hash includes all variadic args:
for (const auto& type_arg : key.variadic_type_args) {
    h ^= std::hash<TypeIndex>{}(type_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
}
```

#### 6. Multiple Levels of Template Specialization (template<> template<> template<typename T>)
**Problem**: Nested template specializations require multiple keys
- `template<> template<> template<typename T> struct Foo<T*>::type`
- Each level needs its own instantiation key
- Dependencies between levels

**Related Fields**:
- `parent_key` - Pointer to parent template's instantiation key
- `nesting_level` - Depth in the nesting hierarchy (0 = top-level)

**Implementation**:
```cpp
// Outer template: Foo<T*>
TemplateInstantiationKey outer_key;
outer_key.template_name = "Foo"_sh;
outer_key.type_args.push_back(int_pointer_type_index);
outer_key.nesting_level = 0;
outer_key.parent_key = nullptr;

// Inner template: Foo<T*>::Inner<U>
TemplateInstantiationKey inner_key;
inner_key.template_name = "Inner"_sh;
inner_key.type_args.push_back(some_type_index);
inner_key.nesting_level = 1;
inner_key.parent_key = &outer_key;  // Link to parent

// This creates a chain: inner -> outer, enabling proper dependency tracking
};
```

#### 7. Type Aliases in Template Arguments (std::vector<T> where vector is alias)
**Problem**: Template arguments through type aliases
- Alias resolution chains: `template<typename T> using Vec = std::vector<T>` then `Vec<int>`
- Need to resolve alias to underlying template
- Example: `template<typename T> using Ptr = T*; template<typename T> struct Container { using type = Ptr<T>; };`

**Related Fields**:
- `alias_resolution` (optional) - Stores the chain: alias -> resolved key
- `is_alias_instantiation` - Flag to quickly check if this is an alias

**Implementation**:
```cpp
// For: template<typename T> using Vec = std::vector<T>;
//      Vec<int> x;
// Should resolve to: std::vector<int>

TemplateInstantiationKey alias_key;
alias_key.template_name = "Vec"_sh;
alias_key.type_args.push_back(int_type_index);
alias_key.is_alias_instantiation = true;

// Record what this alias resolves to
alias_key.alias_resolution = AliasResolution{
    .alias_name = "Vec"_sh,
    .alias_params = {int_type_index},  // Vec<int>
    .resolved_key = underlying_key     // std::vector<int>
};

// Cache both keys pointing to same instantiation:
// cache[Vec<int>] -> instantiation
// cache[vector<int>] -> same instantiation
```

#### 8. Template Arguments with CV Qualifiers (const T, volatile T, const volatile T)
**Problem**: CV qualifiers on template arguments affect type identity
- `Foo<const int>` is different from `Foo<int>`
- CV qualifiers must be part of the key

**Related Fields**:
- `type_args` - The TypeIndex captures the fully qualified type including CV qualifiers
- Type system ensures `const int` has different TypeIndex than `int`

**Implementation**:
```cpp
// Type system already handles CV qualifiers:
// const int -> TypeIndex A
// int -> TypeIndex B  (A != B)

TemplateInstantiationKey key1;
key1.template_name = "Foo"_sh;
key1.type_args.push_back(const_int_type_index);  // const int

TemplateInstantiationKey key2;
key2.template_name = "Foo"_sh;
key2.type_args.push_back(int_type_index);        // int

// key1 != key2 because type_args differ
// Hash automatically differentiates via TypeIndex hash
```

#### 9. Template Arguments with Pointer/Reference Levels (T**, T&&, T&*)
**Problem**: Complex pointer/reference combinations
- `Foo<T**>` is different from `Foo<T*>` and `Foo<T>`
- Must track multi-level pointers accurately

**Related Fields**:
- `type_args` - TypeIndex captures complete type with pointer/reference levels
- Type system treats `int**`, `int*`, `int` as distinct types with different TypeIndex values

**Implementation**:
```cpp
// Type system already tracks:
// int** -> TypeIndex A
// int*  -> TypeIndex B  
// int&  -> TypeIndex C
// int   -> TypeIndex D
// All are distinct!

TemplateInstantiationKey key;
key.template_name = "Container"_sh;
key.type_args.push_back(int_pointer_pointer_type_index);  // T = int**

// Key naturally differentiates all combinations:
// Container<int**> != Container<int*> != Container<int&> != Container<int>
```
- `T&` - lvalue reference (is_reference=true)
- `T**&` - reference to pointer (pointer_depth=2, is_reference=true)
- `T::*` - pointer to member (member_pointer_kind != None)

#### 10. Non-Type Template Arguments as Expressions (template<int N = 42+5>)
**Problem**: Compile-time constant expressions as template arguments
- `template<int N = sizeof(T)> struct Foo;`
- `template<int N = alignof(T)> struct Bar;`
- Need to evaluate expression to constant value before creating key

**Related Fields**:
- `value_arguments` - Simple evaluated values (int64_t)
- `evaluated_value_args` - Complex evaluated expressions with type info
- `unevaluated_expressions` - Fallback if evaluation fails (SFINAE)

**Implementation**:
```cpp
// For: template<int N = sizeof(T)> struct Foo {};
//      Foo<> x;  // Uses sizeof(T) as N

// Step 1: Evaluate the expression
auto result = ConstExprEvaluator::evaluate(sizeof_expr);

TemplateInstantiationKey key;
key.template_name = "Foo"_sh;

if (result.has_value()) {
    // Simple case: store the evaluated value
    key.value_arguments.push_back(result->value);
} else if (is_complex_expression) {
    // Complex case: store full evaluation info
    key.evaluated_value_args.push_back(NonTypeConstant{
        .value = result->value,
        .type = result->type,
        .is_evaluated = true
    });
} else {
    // Evaluation failed: store for SFINAE
    key.unevaluated_expressions.push_back(expr_ast);
}
```

#### 11. Function Type Template Arguments (template<typename R, typename... Args> R(*)(Args...))
**Problem**: Template arguments representing function types
- Function signatures as template parameters
- Example: `template<typename T> struct FunctionPtr { using type = void(*)(T*); };`

**Related Fields**:
- `type_args` - Stores TypeIndex for function types
- Function types have unique TypeIndex just like any other type

**Implementation**:
```cpp
// Function types get their own TypeIndex in gTypeInfo
// void(*)(int*, float&) -> TypeIndex 1234 (unique)

TemplateInstantiationKey key;
key.template_name = "FunctionPtr"_sh;
key.type_args.push_back(func_type_index);  // TypeIndex for void(*)(T*)

// The TypeInfo for function types stores signature details:
struct TypeInfo {
    Type type;  // Type::Function
    std::optional<FunctionTypeInfo> function_info;  // Signature details
};

struct FunctionTypeInfo {
    TypeIndex return_type;              // void
    ChunkedVector<TypeIndex> param_types;  // [int*, float&]
    bool is_variadic;
};
```

#### 12. Array Type Template Arguments (T[N], T[M][N])
**Problem**: Array types with compile-time dimensions
- `template<typename T, int N> struct Array { T data[N]; };`
- Multiple dimensions: `T[N][M]`

**Related Fields**:
- `type_args` - TypeIndex captures the complete array type with dimensions
- Array types like `int[5][10]` get unique TypeIndex values

**Implementation**:
```cpp
// Type system creates unique TypeIndex per array signature:
// int[5]     -> TypeIndex 100
// int[10]    -> TypeIndex 101
// int[5][10] -> TypeIndex 102

TemplateInstantiationKey key;
key.template_name = "Array"_sh;
key.type_args.push_back(array_type_index);  // Complete array type

// For template: template<typename T, int N> struct Array {}
//              Array<int[5][10], 100>
key.type_args.push_back(array_of_int_5_10_index);
key.value_arguments.push_back(100);  // N = 100
```

#### 13. Lambda Capture Types (decltype(lambda), closure types)
**Problem**: Lambda types as template arguments
- `template<typename F> void call(F f); call([](int x) { return x; });`

**Related Fields**:
- `type_args` - Stores TypeIndex for lambda types (just like any other type)

**Implementation**:
```cpp
// Lambdas are treated as regular user-defined types:
// auto lambda = [](int x) { return x; };
// TypeIndex lambda_type = getOrCreateType("<lambda_123>_main_line_42");

TemplateInstantiationKey key;
key.template_name = "call"_sh;
key.type_args.push_back(lambda_type_index);

// No special handling needed - lambdas work via TypeIndex like any other type
// Key ensures: call<lambda_123> != call<lambda_456>
```

#### 14. SFINAE and Failed Instantiations
**Problem**: Substitution failures should not create cache entries
- `template<typename T> struct SFINAE<T, typename T::iterator>` - fails if T has no iterator

**Related Fields**:
- `unevaluated_expressions` - Stores failed expression evaluations
- All key fields participate in the hash for failed instantiation cache

**Implementation**:
```cpp
// Separate cache for failed instantiations
std::unordered_map<TemplateInstantiationKey, std::string, TemplateInstantiationKeyHash> gFailedInstantiations;

// In TemplateRegistry:
std::optional<ASTNode> tryGetInstantiation(const TemplateInstantiationKey& key) {
    // Check success cache first
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
- `struct Foo { template<typename T> struct Bar; };` vs `namespace Ns { template<typename T> struct Bar; };`

**Related Fields**:
- `namespace_qualifiers` - Namespace path (e.g., ["std", "detail"])
- `enclosing_class` - TypeIndex of enclosing class for member templates
- `scope_unique_key` - Combined hash for quick comparison

**Implementation**:
```cpp
// std::vector<int>
TemplateInstantiationKey key1;
key1.template_name = "vector"_sh;
key1.namespace_qualifiers = {"std"};  // std::vector
key1.type_args.push_back(int_type_index);

// Foo::vector<int> (class member template)
TemplateInstantiationKey key2;
key2.template_name = "vector"_sh;
key2.enclosing_class = foo_class_type_index;  // Foo::vector
key2.type_args.push_back(int_type_index);

// Different keys -> separate cache entries
assert(key1 != key2);  // Different scopes!
```

#### 16. Out-of-Line Template Member Definitions (template<typename T> void Foo<T>::method())
**Problem**: Template members defined outside class
- Instantiation happens with template arguments but definition is elsewhere

**Related Fields**:
- `enclosing_class` - Links out-of-line definition to its class

**Implementation**:
```cpp
// Out-of-line: template<typename T> void Foo<T>::method() { ... }
TemplateInstantiationKey key;
key.template_name = "method"_sh;
key.enclosing_class = foo_template_type_index;  // Foo<T>
key.type_args.push_back(concrete_type_index);   // T = int

// Key links out-of-line definition to the class instantiation
```

#### 17. Template Instantiation with constexpr Evaluation
**Problem**: Constant evaluation during instantiation
- `template<int N> struct Array { static constexpr int size = N * 2; };`

**Related Fields**:
- `value_arguments` - Stores evaluated constant values
- `evaluated_value_args` - For complex expressions with type info

**Implementation**:
```cpp
// Step 1: Evaluate all expressions to constants
for (auto& arg : template_args) {
    if (arg.is_value && !arg.is_evaluated) {
        auto result = ConstExprEvaluator::evaluate(arg.expression);
        if (!result) return failure;  // SFINAE
        arg.value = result->value;
        arg.is_evaluated = true;
    }
}

// Step 2: Create key with evaluated values only
TemplateInstantiationKey key;
key.template_name = "Array"_sh;
key.value_arguments.push_back(evaluated_n);  // N = 10 (from N*2 where N=5)

// Key contains only constants, never expressions

// Now create TemplateInstantiationKey with evaluated values
```

#### 18. Template Instantiation with Concepts (template<typename T> requires Integral<T>)
**Problem**: Requires clauses affect instantiation
- Templates can't be instantiated if concept doesn't satisfy
- Example: `template<typename T> requires std::is_integral_v<T> struct Foo;`

**Related Fields**:
- `satisfied_concepts` - List of concepts that must be satisfied
- `concepts_evaluated` - Flag indicating concept check completed

**Implementation**:
```cpp
// Step 1: Evaluate concepts
TemplateInstantiationKey key;
key.template_name = "Foo"_sh;
key.type_args.push_back(int_type_index);

// Check if T satisfies Integral concept
if (ConceptRegistry::satisfies(int_type_index, "Integral")) {
    key.satisfied_concepts.push_back("Integral"_sh);
    key.concepts_evaluated = true;
    // Proceed with instantiation
} else {
    // SFINAE: Don't cache, don't instantiate
    return std::nullopt;
}
```

#### 19. Default Template Arguments Not Provided (template<typename T = int> struct Foo)
**Problem**: Default arguments vs explicit arguments
- Need to distinguish `Foo<int>` vs `Foo<>` (using default)

**Related Fields**:
- `args_are_defaults` - Bitmask tracking which arguments used defaults

**Implementation**:
```cpp
TemplateInstantiationKey key;
key.template_name = "Foo"_sh;
key.type_args.push_back(int_type_index);

// For: Foo<> (using default int)
key.args_are_defaults[0] = true;  // Arg 0 was defaulted

// For: Foo<int> (explicit)
key.args_are_defaults[0] = false;  // Arg 0 was explicit

// Different keys -> separate cache entries
// This allows different behavior for explicit vs defaulted args
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

**Related Fields**:
- `base.scope_hash` - For quick scope filtering (in Base)
- `ext->specialization_rank` - Higher = more specialized (in Extension, allocated for partial specs)

**Implementation**:
```cpp
// Primary template: template<typename T> struct Foo {};
// specialization_rank = 0 (in base struct, no extension needed)

// Partial specialization: template<typename T> struct Foo<T*> {};
// extension->specialization_rank = 1 (extension allocated)

// Overload resolution:
ChunkedVector<std::pair<TemplateInstantiationKey, ASTNode>> candidates;

// Find all matching keys with same scope
for (const auto& [key, node] : gTemplateRegistry.instantiations_) {
    if (key.base.template_name == name && 
        key.base.scope_hash == current_scope_hash &&
        matchesArgs(key, args)) {
        candidates.push_back({key, node});
    }
}

// Sort by specialization_rank from extension
std::vector<decltype(candidates)::value_type> candidates_vec(
    candidates.begin(), candidates.end());
std::sort(candidates_vec.begin(), candidates_vec.end(),
          [](const auto& a, const auto& b) {
              int rank_a = a.first.ext ? a.first.ext->specialization_rank : 0;
              int rank_b = b.first.ext ? b.first.ext->specialization_rank : 0;
              return rank_a > rank_b;  // Higher rank = more specialized
          });

return candidates_vec.empty() ? ChunkedVector<ASTNode>() 
                              : ChunkedVector<ASTNode>{candidates_vec[0].second};
}
```

---

## Quick Reference: Fields by Edge Case

| Field | Location | Purpose | Edge Case # |
|-------|----------|---------|-------------|
| **BASE KEY** (always present, ~80-100 bytes) ||||
| `template_name` | Base | Base template identifier | All |
| `base_template` | Base | TypeIndex for direct lookup | Core |
| `type_args` | Base | Type arguments (up to 4 inline) | 1, 2, 8, 9, 11, 12, 13 |
| `value_arguments` | Base | Non-type arguments (up to 4 inline) | 4, 10, 17, 19 |
| `flags` | Base | Bitmask: variadic, scoped, nested, etc. | All |
| `scope_hash` | Base | Quick scope check (0 = global) | 15 |
| **EXTENSION** (allocated on demand) ||||
| `variadic_type_args` | Extension | Unlimited type packs | 5 |
| `variadic_value_args` | Extension | Unlimited non-type packs | 5 |
| `template_template_args` | Extension | Template template param names | 1 |
| `namespace_qualifiers` | Extension | Detailed namespace path | 15 |
| `enclosing_class` | Extension | TypeIndex of containing class | 15, 16 |
| `parent_key` | Extension | Pointer to parent template | 6 |
| `nesting_level` | Extension | Nesting depth (0 = top) | 6 |
| `alias_resolution` | Extension | Alias -> actual mapping | 7 |
| `satisfied_concepts` | Extension | List of satisfied concepts | 18 |
| `evaluated_value_args` | Extension | Complex evaluated expressions | 10, 17 |
| `unevaluated_expressions` | Extension | Failed evaluations (SFINAE) | 14 |
| `specialization_rank` | Extension | Higher = more specialized | 3, 20 |

## Implementation Checklist

### Phase 1: Data Structures
- [ ] Split TemplateInstantiationKey into Base + Extension
  - [ ] Implement `TemplateInstantiationBaseKey` with core fields (~80-100 bytes)
    - [ ] template_name, base_template
    - [ ] type_args, value_arguments (InlineVector<4>)
    - [ ] flags bitmask
    - [ ] scope_hash
  - [ ] Implement `TemplateInstantiationExtension` with edge case fields
    - [ ] Add `template_template_args` vector
    - [ ] Add `variadic_type_args` and `variadic_value_args` for unlimited packs
    - [ ] Add `namespace_qualifiers` for scope disambiguation
    - [ ] Add `specialization_rank` for overload resolution
    - [ ] Add `parent_key` for nested templates
    - [ ] Add `args_are_defaults` bitset for default arg tracking
  - [ ] Implement lazy allocation (Extension only when needed)
  - [ ] **Target: 70-80% memory savings for common templates**

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
