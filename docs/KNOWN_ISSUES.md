# Known Issues in FlashCpp

## Variadic Template Partial Specialization Member Access (Issue #1)

### Status
**KNOWN BUG** - Compiler hangs when accessing members of variadic template partial specializations

### Affected Code
- Test file: `tests/test_tuple_standard_way_ret32.cpp`
- Related code: Parser postfix operator loop (Parser.cpp:13979), template instantiation system

### Problem Description

The compiler hangs when compiling code that accesses members of variadic template partial specializations, specifically in patterns like:

```cpp
template<typename... Args>
struct Tuple;

template<typename First, typename... Rest>
struct Tuple<First, Rest...> : Tuple<Rest...> {
    First value;  // Member variable
};

int main() {
    Tuple<int> single;
    single.value = 42;  // HANGS HERE
    return 0;
}
```

### Root Cause Analysis

#### What Works
1. ✓ Simple non-variadic templates
2. ✓ Non-variadic partial specializations (e.g., `Container<T*>`)
3. ✓ Variadic partial specializations WITHOUT member access
4. ✓ Explicit template instantiation of variadic templates

#### What Fails
- ✗ Member access on variadic partial specialization instances (e.g., `obj.member`)

#### Technical Details

The hang occurs in the **postfix operator parsing loop** (Parser.cpp:13979) during member type resolution:

1. Parser successfully parses the variable declaration (`Tuple<int> single`)
2. Parser enters postfix operator loop to process `.value`
3. Code calls `get_expression_type()` to determine the member's type
4. This triggers template pattern matching via `matchSpecializationPattern()`
5. Type resolution for variadic templates causes infinite recursion or gets stuck

**Key Problem Areas:**
- `Parser::get_expression_type()` (Parser.cpp:16356) - recursive type resolution
- `StructTypeInfo::findMemberRecursive()` (AstNodeTypes.cpp:806) - member lookup with inheritance
- Template pattern matching for variadic packs during type resolution
- Interaction between parsing loop and template instantiation

### Why This is Complex

The issue stems from architectural limitations in how the compiler handles variadic templates:

1. **Tightly Coupled Parsing and Type Resolution**: Type resolution happens during parsing in the postfix operator loop, creating circular dependencies.

2. **No Cycle Detection**: The recursive member lookup and template instantiation lack cycle detection for variadic patterns.

3. **Variadic Pack Expansion**: The variadic pack (`Rest...`) creates dynamic inheritance chains that aren't properly tracked during member resolution.

4. **Pattern Matching During Parsing**: Template pattern matching occurs synchronously during expression parsing, blocking forward progress.

### Recommended Long-Term Solution (C++ Standard Compliant)

To properly fix this issue in a standard-compliant way, the following architectural changes are needed:

#### 1. Two-Phase Template Instantiation

**Current**: Single-phase instantiation during parsing  
**Needed**: Separate dependent name lookup from instantiation

```cpp
// Phase 1: Parse template definition, record dependent names
// Phase 2: Instantiate with concrete types, resolve all names
```

**Implementation Steps:**
- Create a "dependent type" marker for uninstantiated variadic templates
- Defer member resolution for dependent types until instantiation phase
- Track dependent expressions separately from concrete expressions

**Reference**: C++20 standard section [temp.dep] on dependent names

#### 2. Lazy Template Member Resolution

**Current**: Eager resolution during postfix operator parsing  
**Needed**: Lazy resolution with memoization

```cpp
class LazyMemberResolver {
    std::unordered_map<TypeIndex, std::optional<MemberInfo>> cache_;
    std::unordered_set<TypeIndex> in_progress_;  // Cycle detection
    
    std::optional<MemberInfo> resolve(TypeIndex type, StringHandle member) {
        if (in_progress_.contains(type)) {
            return std::nullopt;  // Cycle detected
        }
        if (cache_.contains(type)) {
            return cache_[type];
        }
        // ... resolve and cache
    }
};
```

**Benefits:**
- Prevents infinite recursion with cycle detection
- Caches results for performance
- Separates concern from parsing

#### 3. Explicit Instantiation Point Tracking

**Current**: Implicit instantiation during type queries  
**Needed**: Explicit instantiation records with point-of-use tracking

```cpp
struct InstantiationRecord {
    SourceLocation point_of_instantiation;
    TemplateDecl* pattern;
    std::vector<TemplateArgument> args;
    InstantiationStatus status;  // Pending, InProgress, Complete, Failed
};

class InstantiationQueue {
    std::vector<InstantiationRecord> pending_;
    std::unordered_set<InstantiationKey> in_progress_;
    
    void processQueue() {
        while (!pending_.empty()) {
            auto& record = pending_.front();
            if (!in_progress_.contains(record.key())) {
                instantiate(record);
            }
            pending_.pop_front();
        }
    }
};
```

**Benefits:**
- Clear separation between parsing and instantiation
- Better error messages with point-of-instantiation info
- Prevents re-entrant instantiation

#### 4. Variadic Pack Expansion Metadata

**Current**: Pack expansion handled implicitly during pattern matching  
**Needed**: Explicit pack expansion records with full type information

```cpp
struct PackExpansion {
    TemplateParameterDecl* pack_param;
    std::vector<TemplateArgument> expanded_args;
    size_t expansion_index;  // Position in the pack
};

struct VariadicInstantiation {
    std::vector<PackExpansion> expansions;
    std::unordered_map<std::string, TypeIndex> pack_substitutions;
    
    // Build inheritance chain explicitly
    std::vector<TypeIndex> buildInheritanceChain() const {
        std::vector<TypeIndex> chain;
        for (size_t i = expansions.size(); i > 0; --i) {
            chain.push_back(expansions[i-1].expanded_type);
        }
        return chain;
    }
};
```

**Benefits:**
- Explicit tracking of pack expansions
- Clear inheritance chain for recursive patterns
- Easier debugging and error reporting

#### 5. Member Lookup with Inheritance Graph

**Current**: Recursive function traversal  
**Needed**: Graph-based traversal with visited tracking

```cpp
struct InheritanceGraph {
    struct Node {
        TypeIndex type;
        std::vector<std::pair<TypeIndex, size_t>> bases;  // base type + offset
    };
    
    std::unordered_map<TypeIndex, Node> nodes_;
    
    std::optional<MemberInfo> findMember(TypeIndex start, StringHandle name) {
        std::unordered_set<TypeIndex> visited;
        std::queue<std::pair<TypeIndex, size_t>> to_visit;
        to_visit.push({start, 0});
        
        while (!to_visit.empty()) {
            auto [current, offset] = to_visit.front();
            to_visit.pop();
            
            if (visited.contains(current)) continue;  // Cycle prevention
            visited.insert(current);
            
            // Check current type's members
            if (auto member = nodes_[current].findDirectMember(name)) {
                member->offset += offset;
                return member;
            }
            
            // Add bases to queue
            for (auto [base_type, base_offset] : nodes_[current].bases) {
                to_visit.push({base_type, offset + base_offset});
            }
        }
        
        return std::nullopt;
    }
};
```

**Benefits:**
- Prevents infinite loops with visited tracking
- More efficient for complex inheritance
- Easier to visualize and debug

### Implementation Roadmap

#### Phase 1: Immediate Workaround (Low Risk)
- Add cycle detection to `findMemberRecursive()` using a `thread_local` visited set
- Add timeout/depth limit to template instantiation
- Mark variadic partial specialization tests as XFAIL (expected failure)

#### Phase 2: Structural Improvements (Medium Risk)
- Separate type resolution from postfix operator parsing
- Implement lazy member resolution with caching
- Add explicit instantiation queue

#### Phase 3: Architecture Overhaul (High Risk)
- Implement two-phase template instantiation
- Build inheritance graph system
- Add full C++20 dependent name tracking

### Testing Strategy

To verify fixes:

```cpp
// Test 1: Simple variadic template
template<typename T>
struct Tuple<T> { T value; };
Tuple<int> t; t.value = 42;  // Should work

// Test 2: Recursive variadic template
template<typename First, typename... Rest>
struct Tuple<First, Rest...> : Tuple<Rest...> {
    First value;
};
Tuple<int, float> t; t.value = 42;  // Should work

// Test 3: Deep nesting
Tuple<int, float, double, char> t;
t.value = 42;  // Should work with proper cycle detection

// Test 4: Cross-reference
template<typename T>
struct A { B<T> b; };
template<typename T>
struct B { A<T>* a; };
A<int> a; a.b.a->b;  // Should detect cycle
```

### References

1. **C++ Standard (C++20)**: ISO/IEC 14882:2020
   - Section 13.8: Template instantiation and specialization
   - Section 13.8.3: Variadic templates
   - Section 13.9.3: Dependent name resolution

2. **Clang Implementation**:
   - `lib/Sema/SemaTemplate.cpp`: Template instantiation
   - `lib/Sema/SemaLookup.cpp`: Member lookup with dependent bases
   - Use of `InstantiatingTemplate` RAII guard for cycle detection

3. **GCC Implementation**:
   - `gcc/cp/pt.c`: Template processing
   - Use of `CLASSTYPE_BEING_INSTANTIATED` flag

4. **Research Papers**:
   - "Two-Phase Name Lookup in C++ Templates" - Vandevoorde & Josuttis
   - "Variadic Templates in C++0x" - Gregor & Järvi

### Workaround for Users

Until this is fixed, users can work around the issue by:

1. **Avoid member access on variadic templates**: Use accessor functions instead
   ```cpp
   template<typename First, typename... Rest>
   struct Tuple<First, Rest...> : Tuple<Rest...> {
       First value;
       First& getValue() { return value; }  // Use accessor
   };
   ```

2. **Use explicit specializations**: Define specializations for specific arities
   ```cpp
   template<typename T1>
   struct Tuple<T1> { T1 value; };
   template<typename T1, typename T2>
   struct Tuple<T1, T2> : Tuple<T2> { T1 value; };
   // etc.
   ```

3. **Use inheritance workaround**: Store in non-variadic base
   ```cpp
   template<typename T>
   struct TupleBase { T value; };
   
   template<typename First, typename... Rest>
   struct Tuple<First, Rest...> : TupleBase<First>, Tuple<Rest...> {
       using TupleBase<First>::value;
   };
   ```

---

**Last Updated**: 2025-12-24  
**Reported By**: Investigation during template argument parser fix  
**Severity**: High (causes compiler hang)  
**Priority**: Medium (workarounds available, affects advanced use cases)
