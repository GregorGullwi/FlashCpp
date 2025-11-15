# Proposal: Pack Expansion in Member Declarations

**Document Number:** FlashCpp-P0001  
**Date:** 2025-11-15  
**Author:** FlashCpp Language Extensions Team  
**Target:** Future C++ Standard (C++29 or later)  
**Status:** Experimental Implementation in FlashCpp

## Abstract

This proposal introduces pack expansion syntax for member variable declarations in class templates, allowing template parameter packs to be directly expanded into multiple member variables. This provides a cleaner, more intuitive alternative to recursive inheritance or complex metaprogramming techniques when implementing tuple-like data structures.

## Motivation

### Current Approach

Currently, C++ requires complex patterns to create tuple-like structures with variadic templates. The standard approaches include:

#### 1. Recursive Inheritance (Most Common)

```cpp
template<typename... Args>
struct Tuple;

template<>
struct Tuple<> {
    static constexpr int size = 0;
};

template<typename Head, typename... Tail>
struct Tuple<Head, Tail...> : Tuple<Tail...> {
    static constexpr int size = 1 + sizeof...(Tail);
    Head value;
    
    Tuple(Head h, Tail... t) : Tuple<Tail...>(t...), value(h) {}
};
```

**Problems:**
- Complex inheritance hierarchy for simple data storage
- Difficult to understand for beginners
- Requires recursive template instantiation
- Member access requires casting or accessor functions
- Poor debugger visualization

#### 2. Multiple Inheritance from Indexed Bases

```cpp
template<int I, typename T>
struct TupleElement {
    T value;
};

template<typename Indices, typename... Args>
struct TupleImpl;

template<int... Indices, typename... Args>
struct TupleImpl<std::integer_sequence<int, Indices...>, Args...>
    : TupleElement<Indices, Args>... {
    // ...
};
```

**Problems:**
- Even more complex template metaprogramming
- Requires understanding of index sequences
- Name lookup complexity
- Still requires helper templates

### Proposed Approach

```cpp
template<typename... Args>
struct Tuple {
    static constexpr int size = sizeof...(Args);
    Args... values;  // Pack expansion in member declaration
    
    Tuple(Args... args) : values(args)... {}  // Standard pack expansion
};

// Usage
Tuple<int, double, char> t(42, 3.14, 'x');
// Internally expands to:
//   int values0;
//   double values1;
//   char values2;
```

**Advantages:**
- Simple, intuitive syntax
- Direct correspondence between template parameters and members
- No inheritance overhead
- Clear member layout
- Better debugger experience (members are visible)
- Matches mental model of "tuple with N members"

## Proposed Syntax

### Member Declaration

```cpp
template<typename... Args>
struct MyClass {
    Args... member_name;  // Expands to member_name0, member_name1, ...
};
```

### Semantics

When a class template containing a member pack expansion is instantiated:

1. The pattern before `...` is repeated for each type in the pack
2. Each instantiation gets a unique indexed name: `name0`, `name1`, `name2`, etc.
3. Members are laid out sequentially in declaration order
4. Standard alignment and padding rules apply

### Full Example

```cpp
template<typename... Args>
struct Tuple {
    static constexpr int size = sizeof...(Args);
    Args... values;
    
    // Constructor with pack expansion (already standard C++)
    Tuple(Args... args) : values(args)... {}
};

// Instantiation
Tuple<int, double, char> t(10, 3.14, 'x');

// Expands to (conceptually):
struct Tuple_int_double_char {
    static constexpr int size = 3;
    int values0;
    double values1;
    char values2;
    
    Tuple_int_double_char(int arg0, double arg1, char arg2)
        : values0(arg0), values1(arg1), values2(arg2) {}
};
```

## Technical Specification

### Grammar Changes

Modify the member-declarator production:

```
member-declarator:
    declarator virt-specifier-seq_opt pure-specifier_opt
    declarator requires-clause_opt
    declarator brace-or-equal-initializer_opt
    identifier_opt attribute-specifier-seq_opt : constant-expression
    parameter-pack-name ... identifier_opt  // NEW
```

### Name Generation

When expanding `Args... values;` with `Tuple<int, double, char>`:

- Generate: `int values0;`, `double values1;`, `char values2;`
- Names are implementation-defined but must follow the pattern `name + index`
- Indices start at 0 and increment sequentially

### Member Access

Direct member access by index is not provided in this proposal. Access should use:

1. Helper functions (e.g., `get<N>()` template)
2. Structured bindings (C++17)
3. Member pointers with metaprogramming

Future proposals may add indexed access syntax.

### Initialization

Member pack expansion works with member initializers:

```cpp
template<typename... Args>
struct Tuple {
    Args... values;
    
    Tuple(Args... args) : values(args)... {}  // Already standard syntax
};
```

The initializer list `values(args)...` expands to `values0(arg0), values1(arg1), ...`

## Design Decisions

### Why Indexed Names?

Alternative considered: Allow accessing by type (e.g., `tuple.int_value`). 

**Rejected because:**
- Doesn't work when pack has duplicate types
- Requires type-based name mangling
- Conflicts if pack contains types with reserved keywords

Indexed names (`values0`, `values1`) are:
- Always unique
- Simple to implement
- Predictable
- Compatible with existing reflection proposals

### Why Not Allow Direct Indexed Access?

Syntax like `tuple.values[0]` was considered but raises questions:
- Is it compile-time or runtime indexing?
- How does it interact with actual array members?
- Requires significant language complexity

This can be addressed in a follow-up proposal focused on compile-time indexing.

## Impact on Existing Code

### Breaking Changes

**None.** This is a pure extension. Existing code cannot contain `...` in member declarations (currently ill-formed).

### Compatibility

- Does not affect ABI of existing code
- Purely opt-in feature
- No impact on compilation of existing templates

## Implementation Experience

### FlashCpp Compiler

FlashCpp has implemented this feature with the following approach:

1. **Parsing:** Detect `...` after type specifier in member declarations
2. **AST:** Flag `DeclarationNode` with `is_pack_expansion` boolean
3. **Instantiation:** During template instantiation, expand pack into individual members
4. **Name Mangling:** Generate indexed names using `StringBuilder`
5. **Layout:** Standard struct layout rules apply to expanded members

**Testing:**
- Successfully compiles and runs tuple-like structures
- Generates correct member offsets and alignment
- Works with member initialization
- Validated against MSVC-generated object code for layout compatibility

**Code size:** ~100 lines of implementation code across parser and instantiation logic.

## Alternatives Considered

### 1. Standard Library Solution Only

Just provide `std::tuple` and expect users to use it.

**Rejected:** Users often need custom tuple-like structures for domain-specific purposes (coordinates, colors, database records). Forcing recursive inheritance for all such cases is unnecessarily complex.

### 2. Reflection-Based Approach

Wait for reflection (P2320, P1240) and generate members programmatically.

**Rejected:** Reflection doesn't simplify the declaration syntax. This proposal provides immediate usability improvements.

### 3. Allow Arbitrary Names

Let users specify names: `Args... values = {x, y, z};`

**Rejected:** 
- Only works when pack size is known
- Doesn't scale to variadic cases
- Complex error handling

## Future Directions

### Pack Indexing Integration

This proposal complements the C++26 pack indexing feature (P2662R3):

```cpp
template<int I, typename... Args>
auto get(Tuple<Args...>& t) {
    return t.values...[I];  // Access I-th expanded member
}
```

### Reflection Support

With reflection, users could enumerate the generated members:

```cpp
template<typename T>
void print_members() {
    for (auto member : std::meta::members_of(^T)) {
        // Iterate over values0, values1, values2, ...
    }
}
```

## Wording

[Formal wording for the standard would be added here in a real proposal]

## Acknowledgments

- Implemented and validated in FlashCpp compiler
- Inspired by user requests for simpler tuple implementations
- Builds on existing pack expansion semantics

## References

- P2662R3: Pack Indexing (C++26)
- P1858R2: Generalized pack declaration and usage
- P2320R0: The Syntax of Static Reflection
- N4917: C++23 Working Draft

---

## Implementation Notes (FlashCpp-Specific)

### Current Status
- ✅ Parsing and AST representation
- ✅ Template instantiation with expansion
- ✅ Name generation with indexed suffixes
- ✅ Member initialization support
- ✅ Correct struct layout and alignment

### Test Coverage
- Single-type packs
- Multi-type packs with varying sizes
- Pack expansion in constructors
- Alignment and padding verification

### Known Limitations
- No direct indexed access syntax (requires helper functions)
- No interaction with structured bindings (yet)
- Member access requires knowing generated names
