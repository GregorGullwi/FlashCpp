# Namespace Storage Refactoring Plan

## Executive Summary

This document proposes a refactoring of how namespaces are stored and handled in FlashCpp. The current implementation uses `std::vector<StringType<>>` (where `StringType` is either `std::string` or `StackString<32>`) which results in:

1. **Memory fragmentation** - Each namespace path creates temporary vectors
2. **Repeated allocations** - Building namespace paths allocates on every lookup
3. **String concatenation overhead** - Building qualified names requires string operations
4. **Hash computation** - Every lookup re-hashes the same namespace paths

The proposed solution introduces a **NamespaceRegistry** with a handle-based system that provides:
- O(1) namespace path lookups via stable handles
- Zero-allocation namespace operations after initial registration
- Pre-computed qualified name strings
- Efficient parent-child namespace traversal

## Current Implementation Analysis

### Current Types and Usage

```cpp
// Current definition in SymbolTable.h
using NamespacePath = std::vector<StringType<>>;

// Current usage patterns:
NamespacePath build_current_namespace_path() const {
    NamespacePath path;
    for (const auto& scope : symbol_table_stack_) {
        if (scope.scope_type == ScopeType::Namespace) {
            path.push_back(scope.namespace_name);
        }
    }
    return path;  // Allocates new vector every call
}
```

### Problem Areas Identified

1. **In `lookup()` functions** (SymbolTable.h lines 338-444, 471-549):
   - Builds `NamespacePath full_ns_path` on every lookup
   - Creates `std::vector<NamespacePath> checked_ns_paths` for deduplication
   - Performs `push_back()` and `pop_back()` repeatedly

2. **In `insert()` functions** (SymbolTable.h lines 130-264):
   - Calls `build_current_namespace_path()` on every insert
   - Creates temporary `NamespacePath ns_path`

3. **In Parser.cpp namespace handling** (lines 8567-8745):
   - Creates temporary `NamespacePath current_path`
   - Creates `NamespacePath inline_path` for inline namespaces

4. **Qualified name building** (SymbolTable.h lines 939-1034):
   - `buildQualifiedName()` iterates through vector elements
   - Creates StringBuilder output for every call

## Proposed Solution: NamespaceRegistry

### Dependencies on Existing Infrastructure

This refactoring builds on several existing FlashCpp types and utilities:

- **`StringHandle`** (defined in `src/StringTable.h`): A 32-bit handle to an interned string, providing O(1) string comparisons and hash lookups
- **`StringTable`** (defined in `src/StringTable.h`): Global string interning system with `getOrInternStringHandle()` and `getStringView()` methods
- **`StringBuilder`** (defined in `src/ChunkedString.h`): Efficient string builder that commits to the global allocator

### Core Data Structures

```cpp
// Forward declaration
struct NamespaceHandle;

// Namespace entry stored in the registry
struct NamespaceEntry {
    StringHandle name;           // Name of this namespace segment (e.g., "std")
    NamespaceHandle parent;      // Handle to parent namespace (invalid for global)
    StringHandle qualified_name; // Pre-computed "std::chrono::seconds" format
    uint32_t depth;              // Nesting depth (0 for global, 1 for top-level, etc.)
    
    // Optional: cache frequently accessed data
    // uint64_t path_hash;       // Pre-computed hash for fast comparisons
};

// Lightweight handle to a namespace entry
struct NamespaceHandle {
    static constexpr uint32_t INVALID_HANDLE = UINT32_MAX;
    uint32_t index = INVALID_HANDLE;
    
    bool isValid() const { return index != INVALID_HANDLE; }
    bool isGlobal() const { return index == 0; }
    
    bool operator==(NamespaceHandle other) const { return index == other.index; }
    bool operator!=(NamespaceHandle other) const { return index != other.index; }
    bool operator<(NamespaceHandle other) const { return index < other.index; }
};

// Hash support for unordered containers
namespace std {
    template<>
    struct hash<NamespaceHandle> {
        size_t operator()(NamespaceHandle h) const noexcept {
            return static_cast<size_t>(h.index);
        }
    };
}
```

### NamespaceRegistry Class

```cpp
class NamespaceRegistry {
public:
    // Global namespace is always entry 0
    static constexpr NamespaceHandle GLOBAL_NAMESPACE = NamespaceHandle{0};
    
    NamespaceRegistry() {
        // Reserve entry 0 for global namespace
        NamespaceEntry global;
        global.name = StringHandle{};  // Empty name
        global.parent = NamespaceHandle{NamespaceHandle::INVALID_HANDLE};
        global.qualified_name = StringHandle{};  // Empty qualified name
        global.depth = 0;
        entries_.push_back(global);
    }
    
    // Get or create a namespace, returning its handle
    // parent_handle is the parent namespace (use GLOBAL_NAMESPACE for top-level)
    NamespaceHandle getOrCreateNamespace(NamespaceHandle parent_handle, StringHandle name) {
        // Check if this namespace already exists
        auto key = std::make_pair(parent_handle, name);
        auto it = namespace_map_.find(key);
        if (it != namespace_map_.end()) {
            return it->second;
        }
        
        // Create new entry
        NamespaceEntry entry;
        entry.name = name;
        entry.parent = parent_handle;
        entry.depth = parent_handle.isValid() ? getEntry(parent_handle).depth + 1 : 1;
        
        // Build qualified name
        entry.qualified_name = buildQualifiedNameForEntry(parent_handle, name);
        
        NamespaceHandle new_handle{static_cast<uint32_t>(entries_.size())};
        entries_.push_back(entry);
        namespace_map_[key] = new_handle;
        
        return new_handle;
    }
    
    // Get namespace by walking a path from parent
    // E.g., getOrCreatePath(GLOBAL, {"std", "chrono"}) -> handle for std::chrono
    NamespaceHandle getOrCreatePath(NamespaceHandle start, 
                                     std::initializer_list<std::string_view> components) {
        NamespaceHandle current = start;
        for (std::string_view component : components) {
            StringHandle name_handle = StringTable::getOrInternStringHandle(component);
            current = getOrCreateNamespace(current, name_handle);
        }
        return current;
    }
    
    // Overload for vector of StringHandle
    NamespaceHandle getOrCreatePath(NamespaceHandle start,
                                     const std::vector<StringHandle>& components) {
        NamespaceHandle current = start;
        for (StringHandle name_handle : components) {
            current = getOrCreateNamespace(current, name_handle);
        }
        return current;
    }
    
    // Access entry data
    const NamespaceEntry& getEntry(NamespaceHandle handle) const {
        assert(handle.isValid() && handle.index < entries_.size());
        return entries_[handle.index];
    }
    
    // Get the qualified name for a namespace
    std::string_view getQualifiedName(NamespaceHandle handle) const {
        if (!handle.isValid() || handle.isGlobal()) return "";
        return StringTable::getStringView(getEntry(handle).qualified_name);
    }
    
    // Get parent namespace
    NamespaceHandle getParent(NamespaceHandle handle) const {
        if (!handle.isValid() || handle.isGlobal()) return GLOBAL_NAMESPACE;
        return getEntry(handle).parent;
    }
    
    // Build qualified name by appending identifier to namespace
    // E.g., buildQualifiedIdentifier(std_chrono_handle, "seconds") -> "std::chrono::seconds"
    StringHandle buildQualifiedIdentifier(NamespaceHandle ns_handle, StringHandle identifier) const {
        if (!ns_handle.isValid() || ns_handle.isGlobal()) {
            return identifier;
        }
        
        const NamespaceEntry& entry = getEntry(ns_handle);
        StringBuilder sb;
        sb.append(StringTable::getStringView(entry.qualified_name));
        sb.append("::");
        sb.append(StringTable::getStringView(identifier));
        return StringTable::createStringHandle(sb);
    }
    
    // Check if one namespace is an ancestor of another
    bool isAncestorOf(NamespaceHandle potential_ancestor, NamespaceHandle child) const {
        NamespaceHandle current = child;
        while (current.isValid() && !current.isGlobal()) {
            if (current == potential_ancestor) return true;
            current = getParent(current);
        }
        return potential_ancestor.isGlobal();  // Global is ancestor of all
    }
    
    // Get all ancestors (for lookup traversal)
    // Returns handles from child to global (exclusive)
    std::vector<NamespaceHandle> getAncestors(NamespaceHandle handle) const {
        std::vector<NamespaceHandle> result;
        NamespaceHandle current = handle;
        while (current.isValid() && !current.isGlobal()) {
            result.push_back(current);
            current = getParent(current);
        }
        return result;
    }
    
private:
    StringHandle buildQualifiedNameForEntry(NamespaceHandle parent, StringHandle name) {
        if (!parent.isValid() || parent.isGlobal()) {
            return name;  // Top-level namespace, qualified name is just the name
        }
        
        const NamespaceEntry& parent_entry = getEntry(parent);
        StringBuilder sb;
        sb.append(StringTable::getStringView(parent_entry.qualified_name));
        sb.append("::");
        sb.append(StringTable::getStringView(name));
        return StringTable::createStringHandle(sb);
    }
    
    // Storage for namespace entries - provides stable indices
    std::vector<NamespaceEntry> entries_;
    
    // Map from (parent_handle, name) -> handle for quick lookup
    // PairHash is a simple hash combiner:
    //   template<typename T1, typename T2>
    //   struct PairHash {
    //       size_t operator()(const std::pair<T1, T2>& p) const {
    //           size_t h1 = std::hash<T1>{}(p.first);
    //           size_t h2 = std::hash<T2>{}(p.second);
    //           return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    //       }
    //   };
    std::unordered_map<std::pair<NamespaceHandle, StringHandle>, NamespaceHandle,
        PairHash<NamespaceHandle, StringHandle>> namespace_map_;
};

// Global instance
extern NamespaceRegistry gNamespaceRegistry;
```

### Integration with SymbolTable

```cpp
// Updated Scope structure
struct Scope {
    ScopeType scope_type = ScopeType::Block;
    std::unordered_map<std::string_view, std::vector<ASTNode>> symbols;
    ScopeHandle scope_handle;
    NamespaceHandle namespace_handle;  // NEW: replaces StringType<> namespace_name
    
    // Using directives now store handles
    std::vector<NamespaceHandle> using_directives;
    
    // Using declarations: local_name -> (namespace_handle, original_name)
    std::unordered_map<std::string_view, std::pair<NamespaceHandle, std::string_view>> using_declarations;
    
    // Namespace aliases: alias -> target namespace handle
    std::unordered_map<std::string_view, NamespaceHandle> namespace_aliases;
};

// Updated SymbolTable methods
class SymbolTable {
public:
    // New: efficient current namespace tracking
    NamespaceHandle get_current_namespace_handle() const {
        // Walk back through scopes to find innermost namespace
        for (auto it = symbol_table_stack_.rbegin(); it != symbol_table_stack_.rend(); ++it) {
            if (it->scope_type == ScopeType::Namespace) {
                return it->namespace_handle;
            }
        }
        return NamespaceRegistry::GLOBAL_NAMESPACE;
    }
    
    void enter_namespace(NamespaceHandle ns_handle) {
        Scope scope(ScopeType::Namespace, symbol_table_stack_.size());
        scope.namespace_handle = ns_handle;
        symbol_table_stack_.push_back(std::move(scope));
    }
    
    // Overload that creates/gets namespace by name
    void enter_namespace(std::string_view namespace_name) {
        NamespaceHandle parent = get_current_namespace_handle();
        StringHandle name_handle = StringTable::getOrInternStringHandle(namespace_name);
        NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(parent, name_handle);
        enter_namespace(ns_handle);
    }
    
    // Updated namespace_symbols_ map - now keyed by handle
    std::unordered_map<NamespaceHandle, 
        std::unordered_map<StringHandle, std::vector<ASTNode>>> namespace_symbols_;
};
```

## Migration Plan

### Phase 1: Add NamespaceRegistry (Non-Breaking)

1. Create `NamespaceRegistry.h` with the new types
2. Create global `gNamespaceRegistry` instance
3. Add `NamespaceHandle namespace_handle` to `Scope` struct alongside existing `namespace_name`
4. Initialize both fields during `enter_namespace()` calls

**Testing**: All existing tests should pass unchanged.

### Phase 2: Update SymbolTable Internal Usage

1. Change `namespace_symbols_` key from `NamespacePath` to `NamespaceHandle`
2. Update `lookup()` and `insert()` to use handles internally
3. Keep public API accepting both old and new types
4. Add deprecation warnings on old paths

**Testing**: Run full test suite, compare performance metrics.

### Phase 3: Update Parser

1. Change Parser's namespace tracking to use handles
2. Update inline namespace handling to use `NamespaceHandle`
3. Update qualified name resolution to use registry lookups

**Testing**: Run full test suite with focus on namespace-related tests.

### Phase 4: Remove Legacy Code

1. Remove `NamespacePath` typedef
2. Remove `NamespacePathHash` and `NamespacePathEqual`
3. Remove `build_current_namespace_path()` (replace with `get_current_namespace_handle()`)
4. Remove `buildQualifiedName()` variants that take `NamespacePath`

**Testing**: Ensure no code references removed types.

## Performance Benefits

### Before (Current Implementation)

```cpp
// Every lookup:
NamespacePath full_ns_path;                    // Vector allocation
for (const auto& scope : symbol_table_stack_) {
    if (scope.scope_type == ScopeType::Namespace) {
        full_ns_path.push_back(scope.namespace_name);  // String copy
    }
}
std::vector<NamespacePath> checked_ns_paths;   // Another vector allocation
// ... iteration with pop_back(), etc.
```

### After (Proposed Implementation)

```cpp
// Every lookup:
NamespaceHandle ns = get_current_namespace_handle();  // O(1) walk, no allocation
// Use handle directly in hash map lookup - O(1)
auto it = namespace_symbols_.find(ns);

// To check ancestors:
while (ns.isValid() && !ns.isGlobal()) {
    // Check namespace_symbols_[ns]
    ns = gNamespaceRegistry.getParent(ns);  // O(1), no allocation
}
```

### Memory Impact

| Aspect | Before | After |
|--------|--------|-------|
| Per-lookup allocations | 2+ vectors | 0 |
| Namespace path storage | N × string copies | 1 × handle (4 bytes) |
| Qualified name | Built on demand | Pre-computed |
| Hash computation | O(path length) | O(1) - identity hash |

## Alternative Considered: ChunkedVector Storage

Instead of `std::vector<NamespaceEntry>`, we could use `ChunkedVector<NamespaceEntry>` for guaranteed pointer stability:

```cpp
ChunkedVector<NamespaceEntry, sizeof(NamespaceEntry) * 1024> entries_;
```

**Pros**: 
- No reallocation/copy on growth
- Better cache locality for sequential access

**Cons**:
- `std::vector` already provides stable indices (not pointers)
- Handles use indices, not pointers
- Additional complexity

**Decision**: Use `std::vector` for simplicity; switch to `ChunkedVector` if profiling shows benefits.

## Open Questions

1. **Thread Safety**: Is the registry accessed from multiple threads? If so, consider:
   - Read-write locks for registry modification
   - Or thread-local caches of namespace handles

2. **Serialization**: Do we need to serialize namespace handles? If so:
   - Handles are just indices, serialization is trivial
   - Consider versioning for forward compatibility

3. **Inline Namespaces**: Current design handles inline namespaces via separate handles + using directives. Verify this matches semantic requirements.

## Files to Modify

1. **New**: `src/NamespaceRegistry.h` - Core types and registry class
2. **Modify**: `src/SymbolTable.h` - Update Scope, SymbolTable, remove old utilities
3. **Modify**: `src/Parser.cpp` - Update namespace handling
4. **Modify**: `src/main.cpp` - Add global registry instantiation

## Testing Strategy

1. **Unit Tests**: Add tests for NamespaceRegistry in isolation
2. **Integration Tests**: Ensure all `test_namespace_*.cpp` tests pass
3. **Performance Tests**: Measure lookup/insert times before and after
4. **Memory Tests**: Measure allocations during typical compilation

## Conclusion

This refactoring replaces the inefficient vector-based namespace path representation with a handle-based registry that eliminates per-operation allocations, pre-computes qualified names, and provides efficient namespace hierarchy traversal. The migration can be done incrementally with full backward compatibility during the transition.
