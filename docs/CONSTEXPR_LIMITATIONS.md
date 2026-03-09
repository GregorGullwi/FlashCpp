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

### ⚠️ Array Access Has Partial Support

Several array-related constexpr forms are supported in simple/supported shapes:

- direct array subscripts such as `values[1]`
- array-element member access such as `items[1].value`
- member-array subscripts such as `box.data[1]`

```cpp
struct Container {
    int data[3];
    constexpr Container() : data{1, 2, 3} {}
};

constexpr Container c;
static_assert(c.data[0] == 1);  // ✅ Works in this simple form
```

Array support is still incomplete in more complex cases.

**Known remaining limitations include:**

1. **Inferred array size**: `int arr[] = {1,2,3}` syntax is not parsed correctly in some contexts
2. **Statement-heavy constexpr evaluation**: more complex local/function-driven array cases can still run into broader statement-evaluation limits

**Guidance for array access:** Prefer explicit array sizes and straightforward direct/member array patterns.

### ❌ Pointer Dereference in Constexpr

Pointer dereferencing still has limited/unsupported constexpr support:

```cpp
constexpr int value = 42;
constexpr const int* ptr = &value;
static_assert(*ptr == 42);  // ❌ Pointer constexpr support is still limited
```

### ❌ Dynamic Allocation in Constexpr (`new` / `delete`)

`new`, `new[]`, `delete`, and `delete[]` should currently be treated as unsupported in constexpr evaluation.

```cpp
constexpr int f() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}

static_assert(f() == 42);  // ❌ Not currently supported
```

**Reason:** The parser has AST nodes for these expressions, but the constexpr evaluator does not currently implement `NewExpressionNode` / `DeleteExpressionNode` handling.

**Workaround:** Avoid dynamic allocation in constexpr code for now; prefer direct objects, aggregates, and fixed-size arrays.

### ⚠️ Constexpr Lambdas Have Remaining Capture Limits

Basic constexpr lambdas work, including explicit captures, default local captures (`[=]`, `[&]`), implicit `this` through default member captures in supported shapes, init-captures, multi-statement bodies, simple member reads / constexpr member calls through `this` / `*this` capture, straightforward mutable by-reference local updates, straightforward identifier-based by-reference init-capture alias updates, straightforward mutable shared-object updates through `[this]`, straightforward mutable copy-local updates through `[*this]`, straightforward mutable closure-local state persistence for by-value/init captures across repeated calls to the same lambda object, straightforward return of lambda closure objects from constexpr functions with repeated calls after local initialization, straightforward returned `[*this]` member closures after local aggregate object initialization, and straightforward nested lambdas that capture enclosing lambda/object state in supported shapes. Capture support is still incomplete beyond those supported shapes.

**Still partial in constexpr lambda evaluation:**

- complex interactions through captured locals / `this` / `*this` (for example, relying on richer aliasing/object-identity behavior, non-identifier init-capture aliasing semantics, richer nested closure aliasing/copying behavior, or more advanced member-function dispatch through the captured object)

```cpp
constexpr int base = 10;
constexpr auto ok = [base](int x) { return base + x; };  // ✅ explicit capture supported

constexpr int f() {
	constexpr int x = 40;
	auto also_ok = [=]() { return x + 2; };
	return also_ok();
}

struct S {
    int value = 42;
    constexpr int f() const {
        auto ok = [=]() { return read(); };
        return ok();
    }
    constexpr int read() const { return value; }
};
```

**Workaround:** If you hit a remaining edge case, prefer capturing concrete constexpr values instead of depending on richer `this` / `*this` object behavior.

### ⚠️ Some Constant-Expression Forms Are Still Partial or Unsupported

Some parsed expression kinds are not yet fully handled by the constexpr evaluator.

**Now supported:**

- `noexcept(expr)`
- `offsetof(T, member)` for direct data-member access

**Partial support:**

- fold expressions require template instantiation context
- pack expansions require template instantiation context

**Currently unsupported in constexpr evaluation:**

- `throw` expressions used in expression contexts

These may currently fail with a generic "expression type not supported in constant expressions" error rather than a specialized diagnostic.

## Implementation Details

### How Member Access Works

When evaluating `p1.x` in a constant expression:

1. **Identify the object**: `p1` must refer to a constexpr object/value source
2. **Get the initializer**: Look up `p1` and obtain its constexpr initializer
3. **Resolve the object shape**: Handle constructor-initialized and aggregate-initialized forms
4. **Locate the member**: Find the member `x` in the object initializer data
5. **Extract the member expression**: Get the expression used to initialize `x` (for example `x_val`)
6. **Match parameter to argument when needed**: If constructor parameters are involved, bind the referenced parameter to the corresponding argument
7. **Evaluate the resulting expression**: Recursively evaluate the resulting value expression

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
- ✅ Constexpr member function calls, including multi-statement bodies in supported shapes
- ✅ Constexpr lambdas with explicit captures, default captures, and current supported `this` / `*this` shapes
- ✅ Multi-statement constexpr free functions (`return`, local vars, `if`, `for`, `while`)
- ✅ Multi-statement constexpr lambdas and callable/operator() bodies in supported shapes
- ✅ Nested member access (e.g., `obj.inner.value`)
- ✅ Direct/member array subscript support in current supported shapes
- ✅ `noexcept(expr)` in constexpr evaluation
- ✅ `offsetof(T, member)` for direct data-member access in constexpr evaluation

### Medium
- ⚠️ Constexpr free function calls (basic support exists)
- ⚠️ Inferred array size parsing (`int arr[] = {1,2,3}`)
- ⚠️ Fold expressions / pack expansions require template instantiation context

### Hard
- ❌ Constructor body statement execution
- ❌ Dynamic allocation in constexpr (`new` / `delete`)
- ❌ Rich capture aliasing/object semantics in constexpr lambdas beyond straightforward by-reference locals, straightforward identifier-based by-reference init-capture aliases, straightforward `[this]` / `[*this]` mutation behavior, straightforward repeated-call mutable closure-local state, straightforward returned closure-object state transfer, straightforward returned `[*this]` member closures from local aggregate objects, and straightforward nested lambdas over enclosing state
- ❌ `throw` expressions in constexpr evaluation
- ❌ Complex member initialization chains

## Recommendations

### For Users

1. **Use member initializer lists** instead of constructor body assignments when you need constexpr evaluation
2. **Nested member access is okay in supported shapes** - prefer simple, directly initialized object graphs
3. **Prefer straightforward member functions** - multi-statement bodies now work in supported shapes, but complex object-state mutation is still limited
4. **Array access is partially supported** - prefer explicit sizes and straightforward direct/member array patterns
5. **Use straightforward lambda captures** - explicit captures, straightforward local `&` captures, straightforward identifier-based `&name = other` init-captures, straightforward mutable by-value/init-capture local state, straightforward returned closure objects from constexpr helper functions, straightforward returned `[*this]` member closures from local aggregate objects, local/default member captures, simple `this` / `*this` member reads/calls, straightforward nested lambdas over enclosing captured/member state, and straightforward mutable `[this]` / `[*this]` updates work best
6. **Avoid `new` / `delete` and `throw` expressions in constexpr code** for now

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

// Good: Nested member access
struct Inner { int value; constexpr Inner(int v) : value(v) {} };
struct Outer { Inner inner; constexpr Outer(int v) : inner(v) {} };
constexpr Outer outer{42};
static_assert(outer.inner.value == 42);

// Good: Simple member array access
struct Data { int arr[3]; constexpr Data() : arr{1, 2, 3} {} };
constexpr Data data{};
static_assert(data.arr[1] == 2);
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

// Bad: Dynamic allocation in constexpr
constexpr int f() {
    int* p = new int(42);
    delete p;
    return 42;
}
static_assert(f() == 42);  // Dynamic allocation not supported

// Still risky: richer captured-object aliasing/identity behavior in constexpr lambdas
struct CaptureExample {
    int x = 7;
    constexpr int bump() { x += 1; return x; }
    constexpr int value() {
        auto by_ref = [this]() mutable { return this->bump(); };
        auto by_copy = [*this]() mutable { return this->bump(); };
        return by_ref() + by_copy();
    }
};

// Bad: Complex statement-heavy constexpr member logic
struct Data {
    int arr[3];
    constexpr int getSecond() const {
        int idx = 1;
        return arr[idx];
    }
};
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
