# Constexpr Evaluation Limitations

This document details the current limitations of constexpr evaluation in FlashCpp, particularly regarding member access and constructor evaluation in constant expressions.

## Overview

FlashCpp supports constexpr evaluation for use in `static_assert`, template parameters, and other contexts requiring compile-time constant expressions. However, the implementation has specific limitations around struct/class member access and constructor evaluation.

## What Works

### ✅ Basic Constexpr Variables
```cpp
constexpr int x = 10;
constexpr double pi = 3.14159;
constexpr bool flag = true;
static_assert(x == 10);  // ✅ Works
```

### ✅ Constexpr Expressions
```cpp
constexpr int sum = 10 + 20 + 30;
constexpr int product = 5 * 6;
constexpr bool comparison = (10 > 5);
static_assert(sum == 60);  // ✅ Works
```

### ✅ Constexpr Struct Construction
```cpp
struct Point {
    int x;
    int y;
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
};

constexpr Point p1(10, 20);  // ✅ Works
```

### ✅ Simple Member Access with Member Initializer Lists
```cpp
struct Point {
    int x;
    int y;
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
};

constexpr Point p1(10, 20);
static_assert(p1.x == 10);  // ✅ Works
static_assert(p1.y == 20);  // ✅ Works
```

**Requirements for member access to work:**
1. The struct must be initialized with a constructor call
2. The constructor must use a member initializer list (`: x(x_val), y(y_val)`)
3. The initializer expressions must be simple parameter references

## What Doesn't Work

### ❌ Constructor Body Assignments

Member access does **NOT** work when the constructor assigns values in the body instead of using a member initializer list:

```cpp
struct Point {
    int x;
    int y;
    
    // ❌ This pattern is NOT supported in constexpr evaluation
    constexpr Point(int x_val, int y_val) {
        x = x_val;
        y = y_val;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.x == 10);  // ❌ Error: Not supported
```

**Reason:** The constexpr evaluator does not execute constructor body statements. It only looks at member initializer lists.

**Workaround:** Use member initializer lists:
```cpp
constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}  // ✅ Works
```

### ❌ Complex Initializer Expressions

Only simple parameter references are supported in member initializer lists:

```cpp
struct Rectangle {
    int width;
    int height;
    int area;
    
    // ❌ Complex expression in initializer not supported
    constexpr Rectangle(int w, int h) 
        : width(w), height(h), area(w * h) {}
};

constexpr Rectangle r(10, 20);
static_assert(r.area == 200);  // ❌ Error: Complex expression not evaluated
```

**Reason:** The evaluator only handles direct parameter-to-member mappings. It doesn't recursively evaluate expressions in initializers.

**Supported:**
```cpp
constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}  // ✅ Simple reference
```

**Not Supported:**
```cpp
constexpr Point(int val) : x(val * 2), y(val + 10) {}  // ❌ Expression
```

### ❌ Constexpr Member Functions

Member function calls on constexpr objects are not supported in constant expressions:

```cpp
struct Point {
    int x;
    int y;
    
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
    
    constexpr int sum() const {
        return x + y;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.sum() == 30);  // ❌ Error: Member function calls not supported
```

**Reason:** Evaluating member functions would require:
1. Looking up the function definition
2. Creating a context with `this` bound to the object
3. Executing the function body with constexpr semantics
4. Supporting return values

This is significantly more complex than simple member access.

**Workaround:** Use free functions or compute values during construction:
```cpp
struct Point {
    int x, y, sum;
    constexpr Point(int x_val, int y_val) 
        : x(x_val), y(y_val), sum(x_val + y_val) {}
};

constexpr Point p1(10, 20);
static_assert(p1.sum == 30);  // ✅ Works
```

### ❌ Default Member Initializers

Default member initializers are not considered when a member is not in the constructor's initializer list:

```cpp
struct Config {
    int timeout = 30;  // Default initializer
    int retries;
    
    constexpr Config(int r) : retries(r) {}
    // timeout is not initialized in the list
};

constexpr Config cfg(5);
static_assert(cfg.timeout == 30);  // ❌ Error: Member not found in initializer list
```

**Reason:** The evaluator only looks at explicit member initializers, not default initializers.

**Workaround:** Include all members in the initializer list:
```cpp
constexpr Config(int r) : timeout(30), retries(r) {}  // ✅ Explicit
```

### ❌ Nested Member Access

Accessing members of nested objects is not supported:

```cpp
struct Inner {
    int value;
    constexpr Inner(int v) : value(v) {}
};

struct Outer {
    Inner inner;
    constexpr Outer(int v) : inner(v) {}
};

constexpr Outer obj(42);
static_assert(obj.inner.value == 42);  // ❌ Error: Nested access not supported
```

**Reason:** The evaluator would need to recursively evaluate each level of member access.

### ❌ Array and Pointer Member Access

Array subscripting and pointer dereferencing are not supported:

```cpp
struct Container {
    int data[3];
    constexpr Container() : data{1, 2, 3} {}
};

constexpr Container c;
static_assert(c.data[0] == 1);  // ❌ Error: Array subscript not supported
```

## Implementation Details

### How Member Access Works

When evaluating `p1.x` in a constant expression:

1. **Identify the object**: `p1` must be an identifier referring to a constexpr variable
2. **Get the initializer**: Look up `p1` in the symbol table and get its `ConstructorCallNode` initializer
3. **Find the constructor**: Locate the constructor in the struct definition that matches the argument count
4. **Locate the member**: Find the member `x` in the constructor's member initializer list
5. **Extract the initializer expression**: Get the expression used to initialize `x` (e.g., `x_val`)
6. **Match parameter to argument**: Find which parameter `x_val` refers to and get the corresponding constructor argument
7. **Evaluate the argument**: Recursively evaluate the argument expression (e.g., the literal `10`)

### Why Constructor Body Execution is Complex

Supporting constructor body execution would require:

1. **Statement-level evaluation**: 
   - Assignment statements (`x = value;`)
   - Conditional statements (`if`, `switch`)
   - Loop statements (`for`, `while`)
   - Return statements (for early exit)

2. **State tracking**:
   - Maintaining member values as they're modified
   - Handling member initialization order
   - Supporting member default values

3. **Control flow**:
   - Branching based on conditions
   - Loop iteration with constexpr bounds
   - Exception handling (if supported)

4. **Function calls**:
   - Calling other constexpr functions
   - Handling recursion limits
   - Managing call stacks

This would essentially require a constexpr interpreter for C++ statements, which is a substantial undertaking.

## Future Improvements

Potential areas for enhancement (in order of complexity):

### Easy
- ✅ Simple parameter reference in initializers (implemented)
- ⚠️ Default member initializers
- ⚠️ Literal expressions in initializers (e.g., `x(val * 2)`)

### Medium
- ⚠️ Constexpr free function calls
- ⚠️ Nested member access (one level deep)
- ⚠️ Array element access with constant indices

### Hard
- ❌ Constructor body statement execution
- ❌ Constexpr member function calls
- ❌ Control flow (if/while/for) in constexpr contexts
- ❌ Complex member initialization chains

## Recommendations

### For Users

1. **Use member initializer lists** instead of constructor body assignments when you need constexpr evaluation
2. **Keep initializers simple** - use direct parameter references
3. **Pre-compute values** - put complex calculations in constructor parameters rather than initializers
4. **Use free functions** instead of member functions for constexpr operations

### For Contributors

When extending constexpr support:

1. Start with the evaluator in `src/ConstExprEvaluator.h`
2. Add support for specific expression types incrementally
3. Test thoroughly with both valid and invalid cases
4. Document new capabilities and limitations
5. Consider the compile-time performance impact

## Examples

### ✅ Best Practices

```cpp
// Good: Simple member initializer list
struct Point {
    int x, y;
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
};

// Good: Pre-computed values
constexpr Point p1(5 * 2, 7 + 3);  // Compute at call site
static_assert(p1.x == 10);

// Good: All members initialized
struct Config {
    int timeout;
    int retries;
    constexpr Config(int t, int r) : timeout(t), retries(r) {}
};
```

### ❌ Patterns to Avoid

```cpp
// Bad: Constructor body assignments
struct Point {
    int x, y;
    constexpr Point(int x_val, int y_val) {
        x = x_val;  // Won't work in constexpr evaluation
        y = y_val;
    }
};

// Bad: Complex initializer expressions
struct Rectangle {
    int area;
    constexpr Rectangle(int w, int h) : area(w * h) {}  // Expression not evaluated
};

// Bad: Member function calls
constexpr Point p(10, 20);
static_assert(p.distance() == 22);  // Member function not supported
```

## Related Features

- **Type traits**: Basic type trait intrinsics (`__is_pointer`, `__is_class`, etc.) are supported
- **Template metaprogramming**: Template specialization and SFINAE work independently of constexpr evaluation
- **Constant folding**: The compiler can optimize constant expressions even without full constexpr support

## See Also

- `src/ConstExprEvaluator.h` - Constexpr evaluation implementation
- `docs/TEMPLATE_FEATURES_COMPLETE.md` - Complete template features status
- `tests/test_constexpr_comprehensive.cpp` - Examples of working constexpr code
- `tests/test_constexpr_structs.cpp` - Test cases showing limitations
