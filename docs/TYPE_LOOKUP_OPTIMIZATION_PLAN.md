# Type Lookup Optimization Plan

## Current Approach

Currently, FlashCpp uses string-based lookups for types and templates in several places:

1. **`gTypesByName`**: A global map from `StringHandle` (interned string) to `TypeInfo*`
2. **Template argument matching**: Compares type names as strings
3. **Struct/class lookups**: Uses `StringTable::getOrInternStringHandle()` to convert names to handles before lookup

### Examples of current patterns:

```cpp
// Type lookup by name
auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));

// Template instantiation key generation
StringBuilder().append(template_name).append("_").append(type_name).commit();
```

## Problems with String-Based Approach

1. **Performance**: String interning and hashing have overhead, even with interned strings
2. **Memory**: Storing full qualified names takes more space than numeric indices
3. **Complexity**: Building qualified names requires string concatenation
4. **Fragility**: Typos in string names aren't caught at compile time

## Proposed Solution: Type Index-Based Lookups

### 1. Use TypeIndex Directly

Every `TypeInfo` already has a `type_index_` field. We should use this as the primary lookup key:

```cpp
// Instead of:
std::unordered_map<StringHandle, TypeInfo*> gTypesByName;

// Use:
std::vector<TypeInfo*> gTypesByIndex;  // Index = TypeIndex
std::unordered_map<StringHandle, TypeIndex> gNameToTypeIndex;  // For initial name resolution only
```

### 2. Template Instantiation Cache by TypeIndex

Instead of building string keys for template instantiations:

```cpp
// Current approach (string-based):
StringBuilder sb;
sb.append(template_name).append("_");
for (const auto& arg : template_args) {
    sb.append(getTypeName(arg)).append("_");
}
auto key = sb.commit();

// Proposed approach (TypeIndex-based):
struct TemplateInstantiationKey {
    TypeIndex base_template;
    std::vector<TypeIndex> type_args;
    
    bool operator==(const TemplateInstantiationKey&) const = default;
};

// Custom hash combining TypeIndices
struct TemplateInstantiationKeyHash {
    size_t operator()(const TemplateInstantiationKey& key) const {
        size_t h = std::hash<TypeIndex>{}(key.base_template);
        for (TypeIndex ti : key.type_args) {
            h ^= std::hash<TypeIndex>{}(ti) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

std::unordered_map<TemplateInstantiationKey, TypeIndex, TemplateInstantiationKeyHash> 
    gTemplateInstantiations;
```

### 3. Store TypeIndex in AST Nodes

Currently, `TypeSpecifierNode` stores a `type_index_` but it's not always used for lookups. Ensure all type references use this index:

```cpp
// In TypeSpecifierNode
TypeIndex type_index_;  // Already exists - use it consistently

// When looking up member types:
const TypeInfo* getTypeInfo() const {
    return type_index_ < gTypesByIndex.size() ? gTypesByIndex[type_index_] : nullptr;
}
```

### 4. Function Overload Resolution by TypeIndex

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

### Phase 1: TypeIndex Infrastructure
- [ ] Create `gTypesByIndex` vector alongside existing `gTypesByName`
- [ ] Ensure all TypeInfo registrations populate both
- [ ] Add helper functions: `getTypeInfo(TypeIndex)`, `getTypeIndex(StringHandle)`

### Phase 2: Template Instantiation Cache
- [ ] Implement `TemplateInstantiationKey` with TypeIndex-based hashing
- [ ] Replace string-based template cache keys
- [ ] Update template instantiation code to use new cache

### Phase 3: AST Node Updates
- [ ] Audit all TypeSpecifierNode usages
- [ ] Replace name-based lookups with index-based where TypeIndex is available
- [ ] Add `type_index_` to StructMember and other type-carrying structures

### Phase 4: Function Resolution
- [ ] Store function signatures as `std::vector<TypeIndex>`
- [ ] Update overload resolution to compare TypeIndex vectors
- [ ] Cache function lookup results by signature hash

## Expected Benefits

1. **Performance**: O(1) vector access vs O(log n) or O(1) with hash overhead
2. **Memory**: TypeIndex is 4 bytes vs variable-length strings
3. **Type Safety**: TypeIndex comparisons can't have typos
4. **Simplicity**: No string building/concatenation for type comparisons

## Migration Strategy

1. Keep string-based APIs for backward compatibility during transition
2. Add new TypeIndex-based APIs in parallel
3. Gradually migrate callers to new APIs
4. Deprecate and eventually remove string-based APIs

## Considerations

- **TypeIndex stability**: TypeIndex values are assigned during parsing. Consider if they need to be stable across compilation units (for incremental compilation).
- **Type aliases**: `typedef` and `using` create aliases that should resolve to the same TypeIndex
- **Template parameters**: Template type parameters need special handling (they're not concrete types until instantiation)
