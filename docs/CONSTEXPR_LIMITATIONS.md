# Constexpr Evaluation Limitations

This document details the current limitations of constexpr evaluation in FlashCpp, particularly regarding member access and constructor evaluation in constant expressions.

## Overview

FlashCpp supports constexpr evaluation for use in `static_assert`, template parameters, and other contexts requiring compile-time constant expressions. The implementation supports struct/class member access, constructor evaluation, and constexpr member functions with some limitations.

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

### ✅ Unary Operators
```cpp
constexpr int negative = -42;
static_assert(negative == -42);  // ✅ Works

constexpr int positive = +42;
static_assert(positive == 42);  // ✅ Works

constexpr int double_neg = -(-10);
static_assert(double_neg == 10);  // ✅ Works
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

### ✅ Complex Initializer Expressions
```cpp
struct Rectangle {
    int width;
    int height;
    int area;
    
    constexpr Rectangle(int w, int h) 
        : width(w), height(h), area(w * h) {}
};

constexpr Rectangle r(10, 20);
static_assert(r.area == 200);  // ✅ Works - complex expression in initializer
```

### ✅ Default Member Initializers
```cpp
struct Config {
    int timeout = 30;  // Default initializer
    int retries = 5;
    int max_connections;
    
    constexpr Config(int max_conn) : max_connections(max_conn) {}
};

constexpr Config cfg(100);
static_assert(cfg.timeout == 30);  // ✅ Works - uses default value
static_assert(cfg.retries == 5);   // ✅ Works - uses default value
static_assert(cfg.max_connections == 100);  // ✅ Works
```

### ✅ Constexpr Member Functions
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
static_assert(p1.sum() == 30);  // ✅ Works - member function call
```

**Requirements for member function evaluation:**
1. The struct must be initialized with a constructor call
2. The constructor must use member initializer lists
3. The member function must be declared `constexpr`
4. The member function must have a single return statement

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

### ❌ Complex Member Function Bodies

Only simple member functions with a single return statement are supported:

```cpp
struct Counter {
    int value;
    
    constexpr Counter(int v) : value(v) {}
    
    // ❌ Multiple statements not supported
    constexpr int conditionalSum() const {
        if (value > 0) {
            return value + 10;
        }
        return value;
    }
};
```

**Workaround:** Use ternary operators for simple conditions:
```cpp
constexpr int conditionalSum() const {
    return value > 0 ? value + 10 : value;  // ✅ Works
}
```
4. Supporting return values

This is significantly more complex than simple member access.

### ✅ Nested Member Access (NEW)

Accessing members of nested objects is now supported:

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
static_assert(obj.inner.value == 42);  // ✅ Works - nested access supported!
```

**Note:** Multi-level nesting (e.g., `a.b.c.d`) is supported. The evaluator recursively 
evaluates each level of member access.

### ❌ Array and Pointer Member Access

Array subscripting and pointer dereferencing have limited support:

```cpp
struct Container {
    int data[3];
    constexpr Container() : data{1, 2, 3} {}
};

constexpr Container c;
static_assert(c.data[0] == 1);  // ❌ Error: Member array subscript limited
```

**Note:** Array subscript code has been implemented in `ConstExprEvaluator.h` but is 
blocked by two parser limitations:

1. **Inferred array size**: `int arr[] = {1,2,3}` syntax is not parsed correctly
2. **Multi-statement functions**: Constexpr functions with local variables require 
   statement-level evaluation which is not yet supported

**Workaround for array access:** Use explicit array size and avoid local arrays:
```cpp
struct Data {
    int arr[3] = {10, 20, 30};
};
// Direct member array access may work in simple cases
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

### Implemented ✅
- ✅ Simple parameter reference in initializers
- ✅ Default member initializers
- ✅ Literal expressions in initializers (e.g., `x(val * 2)`)
- ✅ Unary `-` and `+` operators
- ✅ Constexpr member function calls (single expression body)
- ✅ Nested member access (e.g., `obj.inner.value`) **NEW**
- ✅ Array subscript evaluation (code complete, blocked by parser limitations) **NEW**

### Medium
- ⚠️ Constexpr free function calls (basic support exists)
- ⚠️ Inferred array size parsing (`int arr[] = {1,2,3}`)

### Hard
- ❌ Constructor body statement execution
- ❌ Multi-statement constexpr function bodies
- ❌ Control flow (if/while/for) in constexpr contexts
- ❌ Complex member initialization chains

## Recommendations

### For Users

1. **Use member initializer lists** instead of constructor body assignments when you need constexpr evaluation
2. **Keep struct hierarchies flat** - avoid nested structs for constexpr member access
3. **Use single-expression member functions** - multi-statement bodies are not supported
4. **Prefer scalar members over arrays** for constexpr access

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

// Good: Complex initializer expressions (now supported!)
struct Rectangle {
    int width, height, area;
    constexpr Rectangle(int w, int h) : width(w), height(h), area(w * h) {}
};
constexpr Rectangle rect(4, 5);
static_assert(rect.area == 20);

// Good: Default member initializers (now supported!)
struct DefaultValues {
    int x = 10;
    int y = 20;
};
constexpr DefaultValues dv{};
static_assert(dv.x == 10);

// Good: Constexpr member functions (single expression body)
struct Calculator {
    int a, b;
    constexpr int sum() const { return a + b; }
};
constexpr Calculator calc{3, 4};
static_assert(calc.sum() == 7);
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

// Bad: Nested member access
struct Inner { int value; };
struct Outer { Inner inner; };
constexpr Outer o{.inner = {42}};
static_assert(o.inner.value == 42);  // Nested access not supported

// Bad: Array element access  
struct Data { int arr[3]; };
constexpr Data d{.arr = {1, 2, 3}};
static_assert(d.arr[1] == 2);  // Array subscripting not supported
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
- `tests/test_constexpr_improved.cpp` - Test cases for improved features
- `tests/test_constexpr_member_func.cpp` - Member function constexpr tests
