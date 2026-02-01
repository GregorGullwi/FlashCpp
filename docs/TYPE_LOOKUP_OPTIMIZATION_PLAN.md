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

### Phase 1: Template Instantiation Cache ✅ COMPLETED
- [x] Implement `TemplateInstantiationKeyV2` with TypeIndex + non-type value based hashing
- [x] Create `InlineVector` for efficient storage of template arguments (≤4 args inline)
- [x] Add `TypeIndexArg` for type arguments with CV-qualifier tracking
- [x] Replace string-based template cache keys with numeric keys in `TemplateRegistry`
- [x] Update `try_instantiate_class_template()` to use new V2 cache

### Phase 2: Audit TypeIndex Usage ✅ COMPLETED
- [x] Audit getTypeSizeFromTemplateArgument - removed redundant name lookup when type_index is available
- [x] Audit remaining ~118 gTypesByName.find() usages - Analysis complete:
  - Most are legitimate name-based lookups during parsing (no type_index available yet)
  - Template instantiation lookups optimized via V2 cache (Phase 1)
  - IR code generation lookups work with struct names (type_index would require IR changes)
- [x] Confirm `gTypeInfo[type_index]` is used instead of `gTypesByName.find()` where type_index is available
- [x] No additional redundant lookups found in critical paths

**Key Findings:**
- `gTypesByName.find()` calls fall into three categories:
  1. **Parsing (legitimate)**: Looking up types by name during parse when no type_index exists yet
  2. **Template instantiation (optimized)**: Now uses V2 TypeIndex-based cache
  3. **Code generation (structural)**: Uses struct names from IR ops; optimization would require IR changes
- Direct `gTypeInfo[type_index]` access is already used consistently where type_index is available

### Phase 3: Function Resolution ✅ COMPLETED
- [x] Store function signatures as `std::vector<TypeIndex>` - Implemented `FunctionSignatureKey` using `InlineVector<TypeIndexArg, 8>`
- [x] Update overload resolution to compare TypeIndex vectors - Added `makeTypeIndexArgFromSpec()` and `makeFunctionSignatureKey()` helpers
- [x] Cache function lookup results by signature hash - Added `getFunctionResolutionCache()` and `resolve_overload_cached()`

**New Infrastructure:**
- `FunctionSignatureKey`: TypeIndex-based function signature for caching
- `FunctionSignatureKeyHash`: Hash function for signature keys
- `makeTypeIndexArgFromSpec()`: Convert TypeSpecifierNode to TypeIndexArg
- `makeFunctionSignatureKey()`: Build signature key from function name and argument types
- `getFunctionResolutionCache()`: Global cache for resolved overloads
- `resolve_overload_cached()`: Cached overload resolution with O(1) cache hits

### Phase 4: Template Instantiation Refactoring
- [ ] Extract template instantiation logic into dedicated `TemplateInstantiator` class
- [ ] Create new `src/TemplateInstantiator.h` header file

**Proposed TemplateInstantiator Interface:**
```cpp
class TemplateInstantiator {
public:
    TemplateInstantiator(const std::vector<ASTNode>& params, 
                         const std::vector<TemplateTypeArg>& args);
    
    ASTNode instantiate_function(const ASTNode& template_decl);
    ASTNode instantiate_class(const ASTNode& template_decl);
    ASTNode instantiate_variable(const ASTNode& template_decl);
    
private:
    ASTNode substitute_in_node(const ASTNode& node);
    // Shared substitution logic
};
```

## Implementation Notes (Phase 1)

### New Files Created
- `src/TemplateTypes.h`: Contains `InlineVector`, `TypeIndexArg`, `TemplateInstantiationKeyV2`

### Changes to Existing Files
- `src/TemplateRegistry.h`:
  - Added `makeTypeIndexArg()` and `makeInstantiationKeyV2()` helpers
  - Added `instantiations_v2_` map for V2 caching
  - Added `registerInstantiationV2()` and `getInstantiationV2()` methods

- `src/Parser.cpp`:
  - Added V2 cache lookup at start of `try_instantiate_class_template()`
  - Added V2 cache registration on successful instantiation

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

1. ✅ Add `TemplateInstantiationKeyV2`-based cache alongside existing string cache
2. Gradually migrate template instantiation lookups to new cache
3. Profile to confirm performance improvement before removing string cache

## Considerations

- **TypeIndex stability**: TypeIndex values are assigned during parsing. Consider if they need to be stable across compilation units (for incremental compilation).
- **Type aliases**: `typedef` and `using` create aliases that should resolve to the same TypeIndex
- **Template parameters**: Template type parameters need special handling (they're not concrete types until instantiation)
