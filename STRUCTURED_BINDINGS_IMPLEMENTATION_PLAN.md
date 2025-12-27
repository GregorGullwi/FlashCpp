# Structured Bindings Implementation Plan

**Date**: December 27, 2024  
**Status**: ❌ NOT IMPLEMENTED (despite `__cpp_structured_bindings` macro being defined)

## Current State

### What Fails
```cpp
// Parser error: "Missing identifier: a"
Pair p = {10, 32};
auto [a, b] = p;
```

**Parser Behavior:**
- Sees `auto [` but doesn't recognize structured binding syntax
- Tries to parse `[` as array subscript operator
- Fails with "Missing identifier" error

### What Works (Auto Type Deduction)
All standard auto features work correctly:
- ✅ Basic: `auto x = 42;`
- ✅ Expressions: `auto y = x + 10;`
- ✅ Function returns: `auto p = makePoint();`
- ✅ References: `auto& ref = x;`
- ✅ Const: `const auto c = 50;`
- ✅ Pointers: `auto* ptr = &x;`

**Test files:**
- `test_auto_comprehensive_ret282.cpp` - Verifies all working features
- `test_structured_bindings_not_implemented.cpp` - Shows workaround

## Implementation Plan

### Phase 1: Parsing Phase

**Objective:** Recognize and parse `auto [ identifier-list ] = expr` syntax

**Tasks:**

1. **Detect structured binding pattern in `parse_type_and_name()`**
   - After parsing `auto`, check if next token is `[`
   - Distinguish from array declarations: `auto x[10]` vs `auto [a, b]`
   - Key: structured bindings have `[` immediately after type, before any identifier

2. **Parse identifier list**
   ```cpp
   // In Parser.cpp, parse_type_and_name() or new parse_structured_binding()
   std::vector<std::string_view> binding_identifiers;
   
   // Consume '['
   consume_token();
   
   // Parse identifier list: id1, id2, id3, ...
   do {
       if (current_token.type != TokenType::Identifier) {
           error("Expected identifier in structured binding");
       }
       binding_identifiers.push_back(current_token.value);
       consume_token();
       
       if (current_token.value == ",") {
           consume_token();
       } else {
           break;
       }
   } while (current_token.value != "]");
   
   // Consume ']'
   expect("]");
   ```

3. **Parse initializer**
   - Expect `=` or `{` or `(` after `]`
   - Parse initializer expression: `= expr`, `{expr}`, or `(expr)`
   - For C++17: only `= expr` and `{expr}` are valid
   - For C++20: `(expr)` is also valid

4. **Create AST node**
   ```cpp
   // New node type in AstNodeTypes.h
   class StructuredBindingNode {
   private:
       std::vector<StringHandle> identifiers_;  // Binding names
       ASTNode initializer_;                     // Expression to decompose
       CVQualifier cv_qualifiers_;               // const/volatile
       ReferenceQualifier ref_qualifier_;        // &, &&, or none
       
   public:
       StructuredBindingNode(
           std::vector<StringHandle> ids,
           ASTNode init,
           CVQualifier cv,
           ReferenceQualifier ref
       );
       
       const std::vector<StringHandle>& identifiers() const;
       const ASTNode& initializer() const;
       bool is_const() const;
       bool is_reference() const;
       bool is_rvalue_reference() const;
   };
   ```

**Parsing Examples:**
```cpp
auto [a, b] = expr;              // Basic
const auto [x, y] = expr;        // Const
auto& [r1, r2] = expr;           // Lvalue reference
auto&& [rv1, rv2] = expr;        // Rvalue reference (forwarding)
[[maybe_unused]] auto [i, j] = expr;  // With attributes
```

### Phase 2: Semantic Analysis

**Objective:** Classify binding target and determine decomposition strategy

**Classification Rules (C++17 §11.5):**

1. **Array Type**
   ```cpp
   int arr[2] = {1, 2};
   auto [a, b] = arr;  // Array decomposition
   ```
   - Check if initializer type is array `T[N]`
   - Number of identifiers must equal array size `N`
   - Each binding binds to array element: `identifiers[i]` binds to `array[i]`

2. **Non-Union Class Type (Aggregate)**
   ```cpp
   struct Point { int x; int y; };
   auto [a, b] = Point{1, 2};  // Aggregate decomposition
   ```
   - All non-static data members must be public (direct members only, not inherited in C++17)
   - C++20 allows accessible inherited members
   - Number of identifiers must equal number of members
   - Each binding binds to corresponding member in declaration order

3. **Tuple-Like Type**
   ```cpp
   std::pair<int, int> p{1, 2};
   auto [a, b] = p;  // Tuple-like decomposition
   ```
   - Type must satisfy tuple-like protocol:
     - `std::tuple_size<E>::value` is valid integer constant
     - `std::tuple_element<i, E>::type` is valid type for each `i`
     - Either: `get<i>(e)` is valid (ADL lookup), OR `e.get<i>()` is valid
   - Number of identifiers must equal `tuple_size`
   - Each binding binds to result of `get<i>`

**Implementation in `parse_structured_binding()` or semantic analysis pass:**

```cpp
// Pseudo-code for classification
TypeSpecifierNode initializer_type = deduce_type(initializer_expr);

if (initializer_type.is_array()) {
    // Array decomposition
    size_t array_size = initializer_type.array_size();
    if (identifiers.size() != array_size) {
        error("Number of identifiers must match array size");
    }
    strategy = DecompositionStrategy::Array;
    
} else if (initializer_type.is_class() && !initializer_type.is_union()) {
    // Try aggregate decomposition first
    const StructTypeInfo* struct_info = get_struct_info(initializer_type);
    
    // Check if all non-static data members are public
    bool all_public = true;
    size_t public_member_count = 0;
    for (const auto& member : struct_info->members) {
        if (!member.is_static) {
            if (member.access != AccessSpecifier::Public) {
                all_public = false;
                break;
            }
            public_member_count++;
        }
    }
    
    if (all_public && identifiers.size() == public_member_count) {
        strategy = DecompositionStrategy::Aggregate;
    } else {
        // Try tuple-like decomposition
        if (has_tuple_protocol(initializer_type)) {
            size_t tuple_size = get_tuple_size(initializer_type);
            if (identifiers.size() != tuple_size) {
                error("Number of identifiers must match tuple_size");
            }
            strategy = DecompositionStrategy::TupleLike;
        } else {
            error("Type is not decomposable");
        }
    }
    
} else {
    error("Structured bindings require array or class type");
}
```

**Tuple Protocol Detection:**
```cpp
bool has_tuple_protocol(const TypeSpecifierNode& type) {
    // Check for std::tuple_size specialization
    // Look for: template<> struct tuple_size<Type> { static constexpr size_t value = N; };
    
    // Check for std::tuple_element specialization
    // Look for: template<size_t I> struct tuple_element<I, Type> { using type = ...; };
    
    // Check for get<> function (ADL or member)
    // Look for: template<size_t I> auto get(Type&) or Type::get<I>()
    
    return has_tuple_size && has_tuple_element && has_get;
}
```

### Phase 3: Lowering / Code Generation

**Objective:** Create implicit references or copies and synthesize access code

**General Principle:**
1. Create a hidden variable `e` to hold the initializer (unnamed variable)
2. For each identifier, create a binding (reference or alias)

**Code Generation Strategy:**

#### 3.1 Array Decomposition
```cpp
// Source: auto [a, b] = arr;
// where arr is int[2]

// Lowering (conceptual):
auto&& __e = arr;  // Hidden variable (reference to avoid copy)
alias a = __e[0];  // Reference-like binding to element 0
alias b = __e[1];  // Reference-like binding to element 1
```

**IR Generation:**
```cpp
// For: auto [a, b] = arr;  where arr: int[2]

// 1. Evaluate initializer once and store in hidden variable
%__e = alloc array[2] int32
copy_array %__e from %arr (size: 2 elements)

// 2. Create bindings as references to array elements
// Binding 'a' is like: int& a = __e[0];
%a_ptr = address_of_element %__e index=0
// Register 'a' as reference at %a_ptr in symbol table

// 3. Binding 'b' is like: int& b = __e[1];
%b_ptr = address_of_element %__e index=1
// Register 'b' as reference at %b_ptr in symbol table
```

#### 3.2 Aggregate Decomposition
```cpp
// Source: auto [x, y] = Point{1, 2};
// where Point has members: int x; int y;

// Lowering (conceptual):
auto&& __e = Point{1, 2};  // Hidden variable
alias x = __e.x;           // Reference to member
alias y = __e.y;           // Reference to member
```

**IR Generation:**
```cpp
// For: auto [x, y] = point;  where point: Point {int x; int y;}

// 1. Evaluate initializer and store
%__e = alloc struct Point (size from struct info)
copy_struct %__e from %point (or construct if initializer is constructor)

// 2. Create bindings as references to members
// Get member offsets from struct info
int x_offset = get_member_offset("Point", "x");  // e.g., 0
int y_offset = get_member_offset("Point", "y");  // e.g., 4

// Binding 'x' is like: int& x = __e.x;
%x_ptr = member_address %__e offset=x_offset
// Register 'x' as reference at %x_ptr

// Binding 'y' is like: int& y = __e.y;
%y_ptr = member_address %__e offset=y_offset
// Register 'y' as reference at %y_ptr
```

#### 3.3 Tuple-Like Decomposition
```cpp
// Source: auto [a, b] = pair;
// where pair: std::pair<int, double>

// Lowering (conceptual):
auto&& __e = pair;
alias a = get<0>(__e);  // Call get<0>
alias b = get<1>(__e);  // Call get<1>
```

**IR Generation:**
```cpp
// For: auto [a, b] = pair;  where pair: std::pair<int, double>

// 1. Evaluate initializer and store
%__e = alloc struct pair (size from struct info)
copy_struct %__e from %pair

// 2. Synthesize get<I> calls for each binding
// For tuple-like, we need to call template function get<0>(e)
// This requires template instantiation: get<0, std::pair<int,double>>

// Binding 'a' is like: auto&& a = get<0>(__e);
%e_addr = addressof %__e
%a_result = call_template_function "get" 
            template_args=[integral_constant<0>]
            args=[%e_addr]
            return_type=from tuple_element<0, pair>
// Register 'a' as variable/reference holding %a_result

// Binding 'b' is like: auto&& b = get<1>(__e);
%b_result = call_template_function "get"
            template_args=[integral_constant<1>]
            args=[%e_addr]
            return_type=from tuple_element<1, pair>
// Register 'b' as variable/reference holding %b_result
```

**Tuple-like challenges:**
- Requires template instantiation of `get<I>` functions
- Need to lookup `get` via ADL (Argument Dependent Lookup) or as member function
- Return type must be deduced from `std::tuple_element<I, E>::type`
- Most complex of the three strategies

#### 3.4 Reference Qualifiers

**For `auto& [a, b] = expr;` (lvalue reference):**
- Hidden variable `e` is lvalue reference: `auto& __e = expr;`
- Each binding is lvalue reference to element/member
- Initializer must be lvalue

**For `auto&& [a, b] = expr;` (forwarding reference):**
- Hidden variable `e` is rvalue reference: `auto&& __e = expr;`
- Each binding forwards the value category
- Useful for perfect forwarding scenarios

**For `const auto [a, b] = expr;`:**
- Hidden variable is const: `const auto __e = expr;`
- Each binding is const reference

**Implementation in IRConverter/CodeGen:**
```cpp
void generate_structured_binding(const StructuredBindingNode& node) {
    // 1. Evaluate initializer expression
    auto init_result = generate_expression(node.initializer());
    
    // 2. Create hidden variable for initializer
    std::string hidden_var = generate_unique_name("__structured_binding_e_");
    int hidden_offset = allocate_stack_space(init_result.size);
    
    // Store initializer in hidden variable
    if (node.is_reference()) {
        // Hidden var is reference - just store address
        emit_store_address(hidden_offset, init_result.address);
    } else {
        // Hidden var is value - copy data
        emit_copy(hidden_offset, init_result.address, init_result.size);
    }
    
    // 3. Classify decomposition strategy
    DecompositionStrategy strategy = classify_decomposition(init_result.type);
    
    // 4. Generate bindings based on strategy
    switch (strategy) {
        case DecompositionStrategy::Array:
            generate_array_bindings(node, hidden_offset, init_result.type);
            break;
        case DecompositionStrategy::Aggregate:
            generate_aggregate_bindings(node, hidden_offset, init_result.type);
            break;
        case DecompositionStrategy::TupleLike:
            generate_tuple_bindings(node, hidden_offset, init_result.type);
            break;
    }
}
```

### Phase 4: Symbol Table and Scope

**Each identifier in the binding list creates a new variable:**

```cpp
// Register each identifier in symbol table
for (size_t i = 0; i < identifiers.size(); i++) {
    StringHandle id = identifiers[i];
    
    // Create variable info
    VariableInfo var_info;
    var_info.name = id;
    var_info.type = determine_binding_type(strategy, i, initializer_type);
    var_info.offset = compute_binding_offset(strategy, i, hidden_var_offset);
    var_info.is_reference = true;  // Bindings are always reference-like
    var_info.is_const = node.is_const();
    
    // Add to symbol table
    gSymbolTable.insert(id, var_info);
}
```

### Phase 5: Type Deduction

**The type of each binding is deduced based on the strategy:**

**Array:** Type is array element type
```cpp
// int arr[3]; auto [a, b, c] = arr;
// Type of a, b, c is: int&
```

**Aggregate:** Type is member type  
```cpp
// struct S { int x; double y; }; auto [a, b] = S{};
// Type of a is: int&
// Type of b is: double&
```

**Tuple-like:** Type from `tuple_element`
```cpp
// std::pair<int, string> p; auto [a, b] = p;
// Type of a is: tuple_element<0, pair<int,string>>::type& = int&
// Type of b is: tuple_element<1, pair<int,string>>::type& = string&
```

## Supported Features

### Would Support

✅ **Array decomposition**
```cpp
int arr[3] = {1, 2, 3};
auto [a, b, c] = arr;
```

✅ **Aggregate (struct) decomposition**
```cpp
struct Point { int x, y; };
auto [px, py] = Point{1, 2};
```

✅ **Reference bindings**
```cpp
auto& [a, b] = expr;       // lvalue reference
auto&& [a, b] = expr;      // forwarding reference
const auto& [a, b] = expr; // const lvalue reference
```

✅ **Attributes**
```cpp
[[maybe_unused]] auto [x, y] = point;
```

⚠️ **Tuple-like decomposition** (requires std::tuple_size, std::tuple_element, get<> support)
```cpp
std::pair<int, int> p{1, 2};
auto [a, b] = p;  // Requires template machinery
```
- Would work IF std::pair, std::tuple are available
- Requires proper template instantiation for get<I>
- Need to implement ADL lookup for get functions

### Would NOT Support Initially

❌ **Nested decomposition** (C++20 extension)
```cpp
struct Inner { int x; };
struct Outer { Inner i; int y; };
auto [i, y] = Outer{};  // OK
// But can't do: auto [[x], y] = Outer{};  // Nested - not supported
```

❌ **Bit-field decomposition edge cases**
```cpp
struct Bits { int x : 5; int y : 3; };
auto [a, b] = Bits{};  // Complex - may not work initially
```

❌ **Anonymous union members** (Complex edge case)

## Testing Plan

### Unit Tests

**Array decomposition:**
- `test_structured_binding_array_ret15.cpp` - Basic array
- `test_structured_binding_array_ref.cpp` - Reference binding

**Aggregate decomposition:**
- `test_structured_binding_struct_ret42.cpp` - Basic struct
- `test_structured_binding_struct_const.cpp` - Const binding
- `test_structured_binding_nested_struct.cpp` - Nested struct members

**Reference qualifiers:**
- `test_structured_binding_lvalue_ref.cpp` - auto&
- `test_structured_binding_rvalue_ref.cpp` - auto&&
- `test_structured_binding_const_ref.cpp` - const auto&

**Error cases:**
- `test_structured_binding_wrong_count.cpp` - Wrong number of identifiers
- `test_structured_binding_private_members.cpp` - Non-public members
- `test_structured_binding_union.cpp` - Union (should fail)

### Integration Tests

- Use in range-based for: `for (auto [key, value] : map)`
- Use with function returns: `auto [x, y] = getPoint();`
- Use in if-init: `if (auto [ok, val] = check(); ok)`

## Implementation Files to Modify

1. **src/Parser.cpp** (~500-800 lines)
   - `parse_type_and_name()` or new `parse_structured_binding()`
   - Detect `auto [` pattern
   - Parse identifier list and initializer

2. **src/AstNodeTypes.h** (~50 lines)
   - Add `StructuredBindingNode` class

3. **src/CodeGen.h** or **src/IRConverter.h** (~300-500 lines)
   - Implement decomposition strategies
   - Generate IR for each strategy
   - Handle reference bindings

4. **src/SymbolTable.h** (minor changes)
   - Ensure binding identifiers are registered properly

5. **Tests** (~10-15 new test files)
   - Array, aggregate, reference tests
   - Error case tests

## Estimated Effort

**Total:** ~1500-2000 lines of code changes + tests

- Parsing: 2-3 days (500-800 lines)
- Semantic analysis: 1-2 days (200-300 lines)  
- Code generation: 3-4 days (300-500 lines)
- Testing: 2-3 days (10-15 tests)
- Debug & integration: 2-3 days

**Total estimate:** 10-15 days for full implementation with array and aggregate support.
Tuple-like support adds 3-5 more days due to template complexity.

## References

- C++17 Standard: §11.5 Structured binding declarations
- C++20 Standard: §9.6 Structured binding declarations (with extensions)
- Clang implementation: `clang/lib/Sema/SemaDecl.cpp` - `ActOnDecompositionDeclarator`
- GCC implementation: `gcc/cp/decl.c` - `cp_finish_decomp`
