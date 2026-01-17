# Unused Classes Analysis: LazyMemberResolver and InstantiationQueue

This document provides a detailed analysis of the `LazyMemberResolver` and `InstantiationQueue` classes, which are currently unused in the FlashCpp compiler codebase.

---

## Executive Summary

Both `LazyMemberResolver` and `InstantiationQueue` are designed as "Phase 2" improvements to existing mechanisms. They are fully implemented but not integrated into the compilation pipeline.

| Class | Purpose | Current Equivalent | Status |
|:---|:---|:---|:---|
| `LazyMemberResolver` | Cached, iterative member lookup | `StructTypeInfo::findMemberRecursive` | Ready for integration |
| `InstantiationQueue` | Template instantiation tracking | Ad-hoc tracking in `TemplateRegistry` | Ready for integration |

---

## 1. LazyMemberResolver

### 1.1 Files

*   **Header**: [`src/LazyMemberResolver.h`](file:///c:/Projects/FlashCpp2/src/LazyMemberResolver.h)
*   **Source**: [`src/LazyMemberResolver.cpp`](file:///c:/Projects/FlashCpp2/src/LazyMemberResolver.cpp)

### 1.2 Current Status

A global instance `FlashCpp::gLazyMemberResolver` is declared in `LazyMemberResolver.h` and defined in `LazyMemberResolver.cpp`, but it is **never used** anywhere in the codebase.

### 1.3 Class Overview

```cpp
namespace FlashCpp {

// Result of member lookup with full context
struct MemberResolutionResult {
    const StructMember* member;          // The resolved member (nullptr if not found)
    const StructTypeInfo* owner_struct;  // The struct that owns the member
    size_t adjusted_offset;              // Offset adjusted for inheritance
    bool from_cache;                     // Whether this result came from cache
    // ...
};

class LazyMemberResolver {
public:
    // Resolve a member with caching and cycle detection
    MemberResolutionResult resolve(TypeIndex type_index, StringHandle member_name);

    void clearCache();

    struct Statistics {
        size_t cache_hits;
        size_t cache_misses;
        size_t cycles_detected;
        size_t cache_size;
        double hit_rate() const;
    };
    Statistics getStatistics() const;

private:
    // Internal BFS-based resolution logic
    MemberResolutionResult resolveInternal(TypeIndex type_index, StringHandle member_name);

    std::unordered_map<MemberLookupKey, MemberResolutionResult, MemberLookupKeyHash> cache_;
    std::unordered_set<MemberLookupKey, MemberLookupKeyHash> in_progress_;
    // ...
};

extern LazyMemberResolver gLazyMemberResolver;

} // namespace FlashCpp
```

### 1.4 Comparison with Current Implementation

The existing member lookup is performed by `StructTypeInfo::findMemberRecursive` in [`src/AstNodeTypes.cpp`](file:///c:/Projects/FlashCpp2/src/AstNodeTypes.cpp#L949-987):

```cpp
const StructMember* StructTypeInfo::findMemberRecursive(StringHandle member_name) const {
    RecursionGuard guard(this);
    if (!guard.isActive()) {
        return nullptr;  // Cycle or depth limit detected
    }

    // Check own members
    for (const auto& member : members) {
        if (member.getName() == member_name) {
            return &member;
        }
    }

    // Check base class members
    for (const auto& base : base_classes) {
        // ...
        const StructMember* base_member = base_info->findMemberRecursive(member_name);
        if (base_member) {
            // UNSAFE: Returns pointer to static thread_local variable!
            static thread_local StructMember adjusted_member(StringHandle(), Type::Void, 0, 0, 0, 0);
            adjusted_member = *base_member;
            adjusted_member.offset += base.offset;
            return &adjusted_member;
        }
    }
    return nullptr;
}
```

| Feature | `findMemberRecursive` (Current) | `LazyMemberResolver` (Proposed) |
|:---|:---|:---|
| **Traversal** | Recursive (DFS) | Iterative (BFS via `std::queue`) |
| **Cycle Detection** | `RecursionGuard` (thread-local set + depth limit) | `in_progress_` set per lookup key |
| **Return Type** | `const StructMember*` (raw pointer) | `MemberResolutionResult` (value type) |
| **Offset Adjustment** | Returns pointer to `static thread_local` variable | Returns `adjusted_offset` as a `size_t` by value |
| **Caching** | None | Yes (`cache_` map) |
| **Diagnostics** | None | Statistics (hit rate, cycles detected) |

> [!CAUTION]
> **Critical Bug in Current Implementation**
>
> The current `findMemberRecursive` returns a pointer to a `static thread_local StructMember`. This pointer becomes **invalid** after the next call to `findMemberRecursive` on the same thread. If a caller stores this pointer and uses it after another lookup, it will read stale or corrupted data.

### 1.5 Integration Points (Call Sites)

The `findMemberRecursive` method is called in the following files:

#### [`src/CodeGen.h`](file:///c:/Projects/FlashCpp2/src/CodeGen.h)

*   Line 1748
*   Line 7567
*   Line 7671
*   Line 7789
*   Line 8677
*   Line 9061
*   Line 9126
*   Line 9469
*   Line 9512
*   Line 9582
*   Line 10188
*   Line 14900
*   Line 15267
*   Line 15457
*   Line 16118
*   Line 16718

#### [`src/Parser.cpp`](file:///c:/Projects/FlashCpp2/src/Parser.cpp)

*   Line 18708
*   Line 18781
*   Line 23213

### 1.6 Migration Guide

**Before (current, unsafe):**
```cpp
const StructMember* member = struct_info->findMemberRecursive(member_handle);
if (member) {
    size_t offset = member->offset;  // May be incorrect for base class members!
    // ... use member ...
}
```

**After (proposed, safe):**
```cpp
#include "LazyMemberResolver.h"
// ...
auto result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_handle);
if (result) {
    size_t offset = result.adjusted_offset;  // Always correct
    const StructMember* member = result.member;
    // ... use member ...
}
```

---

## 2. InstantiationQueue

### 2.1 Files

*   **Header**: [`src/InstantiationQueue.h`](file:///c:/Projects/FlashCpp2/src/InstantiationQueue.h)
*   **Source**: [`src/InstantiationQueue.cpp`](file:///c:/Projects/FlashCpp2/src/InstantiationQueue.cpp)

### 2.2 Current Status

A global instance `FlashCpp::gInstantiationQueue` is declared in `InstantiationQueue.h` and defined in `InstantiationQueue.cpp`, but it is **never used** anywhere in the codebase.

### 2.3 Class Overview

```cpp
namespace FlashCpp {

enum class InstantiationStatus {
    Pending,      // Queued but not started
    InProgress,   // Currently being instantiated
    Complete,     // Successfully instantiated
    Failed        // Instantiation failed
};

struct SourceLocation {
    std::string file;
    size_t line;
    size_t column;
};

struct InstantiationKey {
    StringHandle template_name;
    std::vector<TemplateArgument> arguments;
    size_t hash() const;
    bool operator==(const InstantiationKey& other) const;
};

class InstantiationQueue {
public:
    // Enqueue a template instantiation
    void enqueue(const InstantiationKey& key, const SourceLocation& loc);

    // Query status
    bool isComplete(const InstantiationKey& key) const;
    std::optional<TypeIndex> getResult(const InstantiationKey& key) const;
    bool isFailed(const InstantiationKey& key) const;
    std::string getError(const InstantiationKey& key) const;

    // Lifecycle management
    bool markInProgress(const InstantiationKey& key);  // Returns false if cycle detected
    void markComplete(const InstantiationKey& key, TypeIndex result);
    void markFailed(const InstantiationKey& key, const std::string& error);

    // Iteration
    const std::vector<InstantiationRecord>& getPending() const;
    bool hasPending() const;

    void clear();

    struct Statistics {
        size_t pending_count;
        size_t in_progress_count;
        size_t completed_count;
        size_t failed_count;
    };
    Statistics getStatistics() const;

private:
    std::vector<InstantiationRecord> pending_;
    std::unordered_set<InstantiationKey, InstantiationKeyHash> in_progress_;
    std::unordered_map<InstantiationKey, TypeIndex, InstantiationKeyHash> completed_;
    std::unordered_map<InstantiationKey, std::string, InstantiationKeyHash> failed_;
};

extern InstantiationQueue gInstantiationQueue;

} // namespace FlashCpp
```

### 2.4 Analysis

The `InstantiationQueue` provides a robust system for managing template instantiations:

1.  **Queueing**: Pending instantiations are stored in `pending_` with source location information for error reporting.
2.  **Cycle Detection**: The `in_progress_` set prevents infinite recursion when templates instantiate themselves directly or indirectly.
3.  **Caching**: The `completed_` map stores successful instantiations, avoiding redundant work.
4.  **Error Tracking**: The `failed_` map stores error messages for failed instantiations.

### 2.5 Integration Points

The `TemplateRegistry.h` header contains comments referencing `InstantiationQueue`:

```cpp
// See docs/TEMPLATE_ARGUMENT_CONSOLIDATION_PLAN.md for full details
// ...
// History:
//   - Original: Duplicate TemplateArgument in TemplateRegistry.h and InstantiationQueue.h
//   - Consolidation (Tasks 1-4): Unified into single TemplateArgument with TypeIndex support
```

The `TemplateArgument` struct in `TemplateRegistry.h` includes `hash()` and `operator==()` methods specifically noted as "needed for InstantiationQueue".

### 2.6 Migration Guide

**Proposed Usage Pattern:**

```cpp
#include "InstantiationQueue.h"

// When a template instantiation is requested:
InstantiationKey key;
key.template_name = template_name_handle;
key.arguments = /* ... */;

// Check if already done
if (auto result = FlashCpp::gInstantiationQueue.getResult(key)) {
    return *result;  // Use cached result
}

// Check for failure
if (FlashCpp::gInstantiationQueue.isFailed(key)) {
    reportError(FlashCpp::gInstantiationQueue.getError(key));
    return error_type_index;
}

// Mark as in progress (cycle detection)
if (!FlashCpp::gInstantiationQueue.markInProgress(key)) {
    reportError("Recursive template instantiation detected");
    return error_type_index;
}

// Perform instantiation
try {
    TypeIndex result = performInstantiation(key);
    FlashCpp::gInstantiationQueue.markComplete(key, result);
    return result;
} catch (const InstantiationError& e) {
    FlashCpp::gInstantiationQueue.markFailed(key, e.what());
    throw;
}
```

---

## 3. Recommendations

### 3.1 For LazyMemberResolver

1.  **Replace all calls** to `struct_info->findMemberRecursive(name)` with `gLazyMemberResolver.resolve(type_index, name)`.
2.  **Update callers** to use `MemberResolutionResult` instead of raw `StructMember*`.
3.  **Consider deprecating** `StructTypeInfo::findMemberRecursive` after migration.

### 3.2 For InstantiationQueue

1.  **Identify the current template instantiation flow** in `Parser.cpp` and/or `TemplateRegistry.h`.
2.  **Integrate `gInstantiationQueue`** to manage instantiation lifecycle.
3.  **Add source location tracking** for better error messages.

### 3.3 Testing

Both classes should be tested with:
*   **Inheritance hierarchies** (including multiple inheritance)
*   **Recursive templates** (e.g., CRTP patterns)
*   **Variadic templates** (e.g., `std::tuple`)

---

## 4. Consolidation Plan

To simplify the project structure and reduce the number of small source files, the global instances for these classes can be moved to `main.cpp` since they currently only serve as definitions for the `extern` declarations in the headers.

### 4.1 Implementation Steps

1.  **Modify `src/main.cpp`**:
    Add the definitions for the global instances near the other global registries (around line 42):
    ```cpp
    #include "LazyMemberResolver.h"
    #include "InstantiationQueue.h"

    // ...

    NamespaceRegistry gNamespaceRegistry;
    FlashCpp::LazyMemberResolver gLazyMemberResolver;
    FlashCpp::InstantiationQueue gInstantiationQueue;
    ```

2.  **Delete redundant files**:
    *   `src/LazyMemberResolver.cpp`
    *   `src/InstantiationQueue.cpp`

### 4.2 Build System Updates

#### [`Makefile`](file:///c:/Projects/FlashCpp2/Makefile)
Remove the deleted files from `TEST_SOURCES` (line 54) and `MAIN_SOURCES` (line 57).

#### [`FlashCpp.vcxproj`](file:///c:/Projects/FlashCpp2/FlashCpp.vcxproj)
Remove the `<ClCompile>` entries for the deleted files:
*   Line 171: `<ClCompile Include="src\InstantiationQueue.cpp" />`
*   Line 172: `<ClCompile Include="src\LazyMemberResolver.cpp" />`

---

## 5. References

*   `KNOWN_ISSUES.md` (referenced in both headers, but **file not found** in repository)
*   `docs/TEMPLATE_ARGUMENT_CONSOLIDATION_PLAN.md` (referenced in `TemplateRegistry.h`)
