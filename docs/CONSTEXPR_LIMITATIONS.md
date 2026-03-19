# Constexpr Evaluation Limitations

This document details the current limitations of constexpr evaluation in FlashCpp, particularly regarding member access and constructor evaluation in constant expressions.

## Overview

FlashCpp supports constexpr evaluation for use in `static_assert`, template parameters, and other contexts requiring compile-time constant expressions. The implementation supports struct/class member access, constructor evaluation, and constexpr member functions with some limitations.

**Standard boundary:** this document tracks FlashCpp against **C++20 constexpr**.
It does not treat `throw` / `try` / `catch` during constant evaluation as a
supported target. If evaluator internals catch exceptions while reporting
errors, that is implementation plumbing, not language-feature support.

## What Works

### ✅ All Primitive Types in Constexpr Variables and Expressions

Every C++ primitive type is supported as a constexpr variable and in arithmetic/comparison expressions:

```cpp
constexpr bool          cv_bool   = true;
constexpr char          cv_char   = 'A';        // 65
constexpr signed char   cv_schar  = -5;
constexpr unsigned char cv_uchar  = 200;
constexpr short         cv_short  = 1000;
constexpr unsigned short cv_ushort = 60000;
constexpr int           cv_int    = -42;
constexpr unsigned int  cv_uint   = 4000000000u;
constexpr long          cv_long   = 100000L;
constexpr unsigned long cv_ulong  = 3000000000UL;
constexpr long long     cv_llong  = -9000000000LL;
constexpr unsigned long long cv_ullong = 10000000000ULL;
constexpr float         cv_float  = 3.14f;
constexpr double        cv_double = 2.718281828;
constexpr long double   cv_ldouble = 1.41421356L;
```

Arithmetic and comparisons work correctly for all of these types including mixed-type
expressions (C++ usual arithmetic conversions are applied):

```cpp
constexpr double result = 3 + 0.14;      // int + double → double ✅
constexpr float  sum    = 3.5f + 2;      // float + int  → double ✅
static_assert(3.14 > 3.0);               // non-integer double comparison ✅
static_assert(3.14f > 3.0f);             // non-integer float comparison  ✅

constexpr unsigned long long big = 18000000000000000000ULL;
static_assert(big > 1LL);               // unsigned long long vs signed ✅
```

Constexpr functions may also return and accept all primitive types, and
C-style casts / `static_cast` inside function bodies work correctly:

```cpp
constexpr double fn(double a, double b) { return a * b; }
static_assert(fn(3.14, 2.0) > 6.0);    // 6.28 > 6.0 ✅

constexpr int fn_cast(double a) { return (int)a; }
static_assert(fn_cast(3.9) == 3);       // truncation ✅
```

**Known limitation — unsigned type width:**
The evaluator stores all unsigned integers as `unsigned long long` (64-bit) internally.
Wrapping arithmetic that depends on the *declared width* (e.g. `unsigned int` wrapping
at 32 bits) will produce a 64-bit result instead:

```cpp
constexpr unsigned int wrap = 1u - 2u;
// C++ standard: 4294967295 (UINT_MAX, wraps at 32 bits)
// FlashCpp:     18446744073709551615 (ULLONG_MAX, wraps at 64 bits) ⚠️
// static_assert(wrap == 4294967295u);  // ⚠️ fails in FlashCpp
```

This only affects expressions where the unsigned wrapping result is then observed
(e.g. in `static_assert`); arithmetic that stays well within range is unaffected.

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

### ✅ Local Aggregate Object Member Access in Constexpr Functions (NEW)
```cpp
struct Point {
    int x;
};

constexpr int f() {
    Point p{42};
    return p.x;
}

static_assert(f() == 42);  // ✅ Works
```

This also includes straightforward nested local aggregate reads in constexpr functions, such as `obj.inner.value`, and straightforward local member-array reads such as `obj.data[1]`.

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

Multi-statement member functions with if/else, loops, and switch now also work. See the "What Doesn't Work" section for details.

### ✅ Break and Continue in Constexpr Loops
```cpp
constexpr int find_first_gt(int n) {
    int result = -1;
    for (int i = 0; i < 100; i++) {
        if (i * i > n) { result = i; break; }
    }
    return result;
}

static_assert(find_first_gt(10) == 4);  // ✅ Works

constexpr int sum_odd(int n) {
    int sum = 0;
    for (int i = 1; i <= n; i++) {
        if (i % 2 == 0) continue;
        sum += i;
    }
    return sum;
}

static_assert(sum_odd(5) == 9);  // ✅ Works
```

### ✅ Switch Statements in Constexpr Functions
```cpp
constexpr int grade(int score) {
    switch (score / 10) {
        case 10:
        case 9:  return 1;  // A — fall-through supported
        case 8:  return 2;  // B
        case 7:  return 3;  // C
        default: return 4;  // F
    }
}

static_assert(grade(95) == 1);  // ✅ Works
static_assert(grade(85) == 2);  // ✅ Works
```

### ✅ Range-Based For Loops in Constexpr Functions
```cpp
constexpr int sum_arr() {
    int arr[] = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int x : arr) { sum += x; }
    return sum;
}

static_assert(sum_arr() == 15);  // ✅ Works

struct Pair { int key; int value; };
constexpr int find_value(int key) {
    Pair pairs[] = {{1, 10}, {2, 20}, {3, 30}};
    for (auto p : pairs) {
        if (p.key == key) return p.value;
    }
    return -1;
}

static_assert(find_value(2) == 20);  // ✅ Works
```

Range-based for loops over local arrays (both primitive and struct types) are supported.
Range-based for over objects with `begin()`/`end()` (e.g., `std::array`, `std::vector`) is not yet supported.

## What Doesn't Work

### ⚠️ Constructor Body Statements Are Partially Supported

Constructor bodies with member assignments, conditionals, loops, and switch statements now work in constexpr:

```cpp
struct Point {
    int x;
    int y;

    constexpr Point(int x_val, int y_val) {
        x = x_val;
        y = y_val;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.x == 10);  // ✅ Works
static_assert(p1.y == 20);  // ✅ Works
```

```cpp
struct Range {
    int lo;
    int hi;

    constexpr Range(int a, int b) {
        if (a < b) { lo = a; hi = b; }
        else       { lo = b; hi = a; }
    }
};

constexpr Range r(9, 2);
static_assert(r.lo == 2 && r.hi == 9);  // ✅ Works
```

More complex constructor-body execution involving complex aliasing or non-trivial call chains is still a remaining limitation.

**Preferred style when practical:** Use member initializer lists:
```cpp
constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}  // ✅ Works
```

### ✅ Complex Member Function Bodies (NEW)

Multi-statement member functions with if/else, loops, and switch are now supported:

```cpp
struct Counter {
    int value;
    
    constexpr Counter(int v) : value(v) {}
    
    constexpr int conditionalSum() const {
        if (value > 0) {
            return value + 10;
        }
        return value;  // ✅ Works
    }
    
    constexpr int classify() const {
        switch (value) {
            case 0: return 0;
            case 1:
            case 2: return 1;
            default: return 2;
        }  // ✅ Works
    }
};
```

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
- member-array subscripts such as `box.data[1]`, including straightforward local aggregate object cases inside constexpr functions

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

1. **Inferred array size in richer contexts**: straightforward local inferred-size arrays now work, including simple local scalar arrays and simple local aggregate-array member reads, but `int arr[] = {1,2,3}` can still fail in more complex parser/evaluator contexts
2. **Range-based for over arrays**: range-based for loops over local arrays now work in constexpr, but over objects with `begin()`/`end()` methods are not yet supported

**Guidance for array access:** Prefer explicit array sizes when practical, but straightforward inferred-size local array patterns are now supported too.

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

Basic constexpr lambdas work, including:
- Explicit captures
- Default local captures (`[=]`, `[&]`)
- Implicit `this` through default member captures in supported shapes
- Init-captures
- Multi-statement bodies
- Simple member reads / constexpr member calls through `this` / `*this` capture
- Straightforward mutable by-reference local updates
- Straightforward identifier-based by-reference init-capture alias updates
- Straightforward mutable shared-object updates through `[this]`
- Straightforward mutable copy-local updates through `[*this]`
- Straightforward mutable closure-local state persistence for by-value/init captures across repeated calls to the same lambda object
- Straightforward return of lambda closure objects from constexpr functions with repeated calls after local initialization
- Straightforward returned `[*this]` member closures after local aggregate object initialization
- Straightforward nested lambdas that capture enclosing lambda/object state in supported shapes

Capture support is still incomplete beyond those supported shapes.

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
- `offsetof(T, member)` for direct and straightforward nested data-member access (for example `offsetof(T, inner.value)`)

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
	- this now includes straightforward local aggregate objects already materialized in bound constexpr evaluation state
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
- ✅ Constexpr member function calls, including multi-statement bodies with `if`, `for`, `while`, and `switch`
- ✅ Constexpr lambdas with explicit captures, default captures, and current supported `this` / `*this` shapes
- ✅ Multi-statement constexpr free functions (`return`, local vars, `if`, `for`, `while`, `switch`)
- ✅ Multi-statement constexpr lambdas and callable/operator() bodies in supported shapes
- ✅ Nested member access (e.g., `obj.inner.value`)
- ✅ Direct and nested member reads from local aggregate constexpr objects inside constexpr functions (e.g., `obj.value`, `obj.inner.value`)
- ✅ Direct/member array subscript support in current supported shapes, including straightforward local aggregate object reads like `obj.data[1]`
- ✅ Straightforward inferred-size local arrays in constexpr functions, including simple scalar reads and simple aggregate-array element member reads
- ✅ Straightforward local aggregate-array element reads in constexpr functions, including nested/member-array compositions like `items[i].inner.value` and `items[i].data[0]`
- ✅ Straightforward loop-driven local array reads in constexpr functions, including `sum += arr[i]` and `sum += items[i].value`
- ✅ Straightforward constructor-body member assignments in constexpr objects (including if/else, for/while, and switch bodies)
- ✅ `noexcept(expr)` in constexpr evaluation
- ✅ `offsetof(T, member)` for direct and straightforward nested data-member access in constexpr evaluation
- ✅ `break` and `continue` statements in constexpr for/while loops
- ✅ `switch` statements with case labels, default label, fall-through, and `break` in constexpr functions
- ✅ Range-based for loops over local arrays (primitive and struct element types) in constexpr functions
- ✅ All primitive types (`bool`, `char`, signed/unsigned integer variants, `float`, `double`, `long double`) in constexpr variables, arithmetic, comparisons, function parameters/return values, and C-style/`static_cast` conversions inside constexpr function bodies
- ✅ Mixed-type arithmetic following C++ usual arithmetic conversions: float/double vs any → double path; unsigned long long vs signed → unsigned path; bool/char/short/int/long/long long → signed path

### Medium
- ⚠️ Constexpr free function calls (basic support exists)
- ⚠️ Inferred array size parsing in richer contexts beyond straightforward local array cases (`int arr[] = {1,2,3}`)
- ⚠️ Fold expressions / pack expansions require template instantiation context
- ⚠️ Range-based for loops over objects with `begin()`/`end()` (e.g., `std::array`, `std::vector`) are not yet supported in constexpr
- ⚠️ Unsigned wrapping arithmetic at the declared type's width: all unsigned values are stored as `unsigned long long` (64-bit) internally, so `unsigned int` wrapping (at 32 bits), `unsigned short` wrapping (at 16 bits), etc. give 64-bit results. Arithmetic that stays within range is unaffected.

### Hard
- ⚠️ Complex constructor body statement execution involving complex aliasing or non-trivial call chains (simple assignments, conditionals, loops, and switch now work)
- ❌ Dynamic allocation in constexpr (`new` / `delete`)
- ❌ Rich capture aliasing/object semantics in constexpr lambdas beyond:
  - straightforward by-reference locals
  - straightforward identifier-based by-reference init-capture aliases
  - straightforward `[this]` / `[*this]` mutation behavior
  - straightforward repeated-call mutable closure-local state
  - straightforward returned closure-object state transfer
  - straightforward returned `[*this]` member closures from local aggregate objects
  - straightforward nested lambdas over enclosing state
- ❌ `throw` expressions in constexpr evaluation
- ❌ Complex member initialization chains

## Recommendations

### For Users

1. **Prefer member initializer lists when practical** - constructor body member assignments, conditionals, loops, and switch all work too, but complex aliasing chains are still more fragile
2. **Nested/member access is okay in supported shapes** - this includes straightforward local aggregate object reads like `obj.value` and `obj.inner.value`; prefer simple, directly initialized object graphs
3. **Multi-statement member functions now work** - if/else, for/while, switch, and break/continue are all supported
4. **Array access is partially supported** - prefer explicit sizes and straightforward direct/member array patterns, including simple local object member-array reads like `obj.data[1]`, straightforward local inferred-size arrays like `int arr[] = {1, 2}`, and straightforward loop-driven reads over supported local arrays
5. **Use straightforward lambda captures** - the following work best:
   - explicit captures
   - straightforward local `&` captures
   - straightforward identifier-based `&name = other` init-captures
   - straightforward mutable by-value/init-capture local state
   - straightforward returned closure objects from constexpr helper functions
   - straightforward returned `[*this]` member closures from local aggregate objects
   - local/default member captures
   - simple `this` / `*this` member reads/calls
   - straightforward nested lambdas over enclosing captured/member state
   - straightforward mutable `[this]` / `[*this]` updates
6. **Avoid `new` / `delete` and `throw` expressions in constexpr code** for now

### For Contributors

When extending constexpr support:

1. Start with the evaluator in `src/ConstExprEvaluator.h`
2. Add support for specific expression types incrementally
3. Test thoroughly with both valid and invalid cases
4. Document new capabilities and limitations
5. Consider the compile-time performance impact

### Architectural Follow-Up Task: Evolve `EvalResult` for Fuller C++20 Constexpr Support

As constexpr support expands toward fuller C++20 object/array/closure semantics, `EvalResult` is carrying increasingly rich recursive state (for example array elements, object member bindings, and callable capture bindings). The current value-heavy representation is still workable, but it is now architectural debt and should be treated as a tracked follow-up task rather than an open-ended future concern.

**Task:** Design and implement a phased `EvalResult` representation refactor that keeps scalar constexpr results cheap while making recursive object/array/callable state more scalable.

**Preferred direction:** Keep simple scalar values inline, but split heavier recursive state into dedicated payload objects instead of turning the entire `EvalResult` into a shared, implicitly aliased graph.

**Draft plan:**

1. **Measure current copy pressure**
   - Identify the hottest `EvalResult` copy paths in constexpr evaluation (binding maps, argument binding, object materialization, array materialization, callable capture state, return propagation).
   - Add lightweight instrumentation or targeted profiling before changing representation.
2. **Separate scalar vs structured state**
   - Keep primitive constant values (`bool` / integer / floating-point) directly embedded in `EvalResult`.
   - Move array/object/callable payloads behind dedicated state nodes or arena-managed payload storage.
3. **Preserve explicit value semantics**
   - Do not switch blindly to `shared_ptr` everywhere.
   - Make copy-vs-share behavior explicit so constexpr by-value copies, closure copies, `[this]` vs `[*this]`, and local object writeback remain correct.
4. **Introduce structured clone-on-write only where needed**
   - If shared backing is introduced for heavy payloads, require explicit detachment before mutation in paths that semantically produce independent state.
5. **Refactor incrementally with regression coverage**
   - Convert one payload family at a time (arrays, then object state, then callable state).
   - Keep targeted regression tests for local object mutation, array element access, constructor materialization, lambda closure copies, and by-reference capture writeback.
6. **Re-evaluate complexity limits and performance after each slice**
   - Confirm that new representation changes do not accidentally make constexpr evaluation slower or more alias-prone in common scalar cases.

**Non-goal for the first slice:** A blanket “everything becomes `shared_ptr`” rewrite. That would be too risky for current constexpr value/copy semantics.

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
// Avoid assuming all constructor body logic is supported
struct Point {
    int x, y;
    constexpr Point(int x_val, int y_val) {
        x = x_val;
        y = y_val;
        // more complex constructor-body logic may still fail in constexpr evaluation
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
