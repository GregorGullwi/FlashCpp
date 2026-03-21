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

**Unsigned type width:**
Unsigned arithmetic wraps at the *declared type's* width:

```cpp
constexpr unsigned int wrap = 1u - 2u;
static_assert(wrap == 4294967295u);  // ✅ wraps at 32 bits (UINT_MAX)

constexpr unsigned int add_overflow = 4294967295u + 1u;
static_assert(add_overflow == 0u);   // ✅ wraps correctly
```

The evaluator masks the result to the correct width after each arithmetic
operation when both operands have known exact types.  When the declared type
cannot be determined (e.g. some template-dependent expressions), the result
may fall back to 64-bit storage; arithmetic that stays well within range is
always unaffected.

Unsigned wrapping for `++` / `--` now also applies the declared-type mask after
the arithmetic step, so `unsigned int`, `unsigned char`, and `unsigned short`
all wrap correctly (e.g. `unsigned int x = UINT_MAX; x++;` wraps to `0`).

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
Range-based for over objects with `begin()`/`end()` member functions is now supported when the methods are `constexpr` and return a member array or pointer. This covers user-defined iterable structs with integral element types. See also the [begin()/end() section](#constexpr-range-based-for-over-beginend-objects-new).

### ✅ Constexpr Recursion

Recursive constexpr functions are fully supported. Both the `if`/`return` style and the
ternary-expression style work at any reasonable depth (the evaluator enforces a 512-level
recursion limit, matching common compilers).

```cpp
// Classic Fibonacci — if/return style
constexpr int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

static_assert(fib(0) == 0);   // ✅
static_assert(fib(1) == 1);   // ✅
static_assert(fib(10) == 55); // ✅
static_assert(fib(15) == 610);// ✅

// Fibonacci — ternary style
constexpr long long fib_t(long long n) {
    return n <= 1 ? n : fib_t(n - 1) + fib_t(n - 2);
}

static_assert(fib_t(10) == 55LL); // ✅

// Factorial
constexpr long long factorial(int n) {
    return n <= 1 ? 1LL : (long long)n * factorial(n - 1);
}

static_assert(factorial(5)  == 120);     // ✅
static_assert(factorial(10) == 3628800); // ✅

// Integer power
constexpr long long power(long long base, int exp) {
    if (exp == 0) return 1LL;
    return base * power(base, exp - 1);
}

static_assert(power(2, 8)  == 256);     // ✅
static_assert(power(10, 6) == 1000000); // ✅
```

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

### ✅ Array Member Brace-Init in Constructor Initializer Lists (NEW)

Member arrays initialized with a brace-init list in a constructor's member initializer list are now supported:

```cpp
struct Triplet {
    int vals[3];
    constexpr Triplet(int a, int b, int c) : vals{a, b, c} {}
    constexpr int get(int i) const { return vals[i]; }
};

constexpr Triplet t(10, 20, 30);
static_assert(t.vals[0] == 10);  // ✅ Works
static_assert(t.vals[2] == 30);  // ✅ Works
static_assert(t.get(1) == 20);   // ✅ Works - member function with literal index
```

Single-element and partial brace-init also correctly zero-fill trailing elements:

```cpp
struct A {
    int arr[4];
    constexpr A() : arr{7} {}     // ✅ arr[0]=7, arr[1..3]=0
};
struct B {
    int arr[5];
    constexpr B() : arr{1,2,3} {} // ✅ arr[0..2]=1,2,3, arr[3..4]=0
};
```

Using a local variable as the array subscript inside a constexpr member function is also supported:

```cpp
struct Container {
    int data[4];
    constexpr Container() : data{1, 2, 3, 4} {}
    constexpr int getSecond() const {
        int idx = 1;
        return data[idx];   // ✅ Works - local variable as subscript
    }
};
```

### ⚠️ Array Access Has Partial Support

Several array-related constexpr forms are supported in simple/supported shapes:

- direct array subscripts such as `values[1]`
- array-element member access such as `items[1].value`
- member-array subscripts such as `box.data[1]`, including straightforward local aggregate object cases inside constexpr functions
- member-array brace-init in constructor initializer lists such as `arr{a, b, c}` with full C++ zero-fill for partial/single-element init
- local variable as array subscript inside constexpr member functions such as `int idx = 1; return arr[idx];`

```cpp
struct Container {
    int data[3];
    constexpr Container() : data{1, 2, 3} {}
};

constexpr Container c{};
static_assert(c.data[0] == 1);  // ✅ Works
```

Array support is still incomplete in more complex cases.

**Known remaining limitations include:**

1. **Inferred array size in richer contexts**: straightforward local inferred-size arrays now work, including simple local scalar arrays and simple local aggregate-array member reads, but `int arr[] = {1,2,3}` can still fail in more complex parser/evaluator contexts
2. **Range-based for over arrays**: range-based for loops over local arrays now work in constexpr, and over objects with `constexpr begin()`/`end()` methods returning a member array or pointer are now also supported (see dedicated section)

**Guidance for array access:** Prefer explicit array sizes when practical, but straightforward inferred-size local array patterns are now supported too.

### ✅ Constexpr Range-Based For over `begin()`/`end()` Objects (NEW)

Range-based for loops over user-defined iterable types with `constexpr begin()` and `constexpr end()` member functions are now supported in the constexpr evaluator. The member functions must be `constexpr` and return either:

1. The member array directly (`return data;`) — the evaluator detects the full array and iterates it, using the `end()` return value as a count constraint when it involves array decay.
2. A pointer via `&data[0]` or `&data[size]` syntax — the evaluator resolves the backing member array and uses the pointer offsets to bound the iteration.

```cpp
// Case A: begin() returns member array directly
struct IntVec3 {
    int data[3];
    constexpr IntVec3() : data{10, 20, 30} {}
    constexpr int* begin() { return data; }
    constexpr int* end()   { return data + 3; }
};

constexpr int sum_vec3() {
    IntVec3 v{};
    int s = 0;
    for (int x : v) s += x;
    return s;
}
static_assert(sum_vec3() == 60);  // ✅ Works

// Case B: begin()/end() with pointer syntax
struct BoundedVec {
    int data[5];
    int size;
    constexpr BoundedVec() : data{2, 4, 6, 8, 10}, size(3) {}
    constexpr int* begin() { return &data[0]; }
    constexpr int* end()   { return &data[size]; }
};

constexpr int sum_bounded() {
    BoundedVec v{};
    int s = 0;
    for (int x : v) s += x;
    return s;
}
static_assert(sum_bounded() == 12);  // ✅ Works (only first 3 elements)

// Template structs with begin()/end() are also supported
template<int N>
struct StaticVec {
    int data[N];
    constexpr int* begin() { return data; }
    constexpr int* end()   { return data + N; }
};

constexpr int sum_static() {
    StaticVec<4> v{{1, 2, 3, 4}};
    int s = 0;
    for (int x : v) s += x;
    return s;
}
static_assert(sum_static() == 10);  // ✅ Works
```

`break`, `continue`, and early `return` inside these loops work correctly.
Nested range-for loops over two objects also work.

**Current limitations:**
- Element type must be an integer scalar (struct elements in the iteration variable are not yet supported due to a pre-existing member-access limitation in the evaluator)
- `data + N` style in member functions (array decay) relies on `origin_var_name` tagging; this works when `begin()` returns the member array directly

### ✅ Basic Pointer Dereference in Constexpr (NEW)

Basic constexpr pointer support is now implemented for named constexpr variables:

```cpp
constexpr int value = 42;
constexpr const int* ptr = &value;
static_assert(*ptr == 42);  // ✅ Works

// Pointer passed as constexpr function argument
constexpr int deref(const int* p) { return *p; }
static_assert(deref(&value) == 42);  // ✅ Works

// Arrow member access through constexpr pointer
struct Point { int x, y; constexpr Point(int a, int b) : x(a), y(b) {} };
constexpr Point p{10, 20};
constexpr const Point* pp = &p;
static_assert(pp->x == 10);  // ✅ Works

// Arrow access in constexpr function
constexpr int sum(const Point* pt) { return pt->x + pt->y; }
static_assert(sum(&p) == 30);  // ✅ Works
```

### ✅ Null Pointer Checks and Pointer Comparisons in Constexpr (NEW)

Null pointer checks and pointer equality comparisons are now supported:

```cpp
constexpr int val = 42;
constexpr const int* ptr = &val;

// ptr == nullptr → false (valid constexpr pointer is always non-null)
static_assert(!(ptr == nullptr));   // ✅ Works
static_assert(ptr != nullptr);      // ✅ Works
static_assert(!(nullptr == ptr));   // ✅ Works

// Pointer equality (same/different variables)
constexpr const int* ptr2 = &val;
static_assert(ptr == ptr2);         // ✅ Works — same variable

constexpr int other = 99;
constexpr const int* ptr3 = &other;
static_assert(ptr != ptr3);         // ✅ Works — different variables

// Logical not: !ptr → false (non-null pointer is truthy)
static_assert(!(!ptr));             // ✅ Works

// Logical and/or with pointers
static_assert(ptr && true);         // ✅ Works
static_assert(ptr || false);        // ✅ Works

// Null check helper function
constexpr bool is_null(const int* p) { return p == nullptr; }
static_assert(!is_null(&val));      // ✅ Works

// Conditional using pointer truthiness
constexpr int deref_or(const int* p, int def) {
    if (p) return *p;
    return def;
}
static_assert(deref_or(&val, 0) == 42);  // ✅ Works
```

**Supported pointer forms:**
- `&named_var` (address-of a named constexpr variable)
- `&arr[i]` (address of array element — produces a pointer with offset)
- `*ptr` (dereference to get the pointed-to value)
- `*(ptr + n)` (dereference with pointer arithmetic)
- `ptr[i]` (pointer subscript — equivalent to `*(ptr + i)`)
- `ptr + n`, `n + ptr`, `ptr - n` (pointer arithmetic)
- `ptr1 - ptr2` (pointer difference — both must point into the same array)
- `ptr->member` (arrow member access through constexpr pointer)
- `(ptr + n)->member` (arrow member access with offset, for struct arrays with object_member_bindings)
- Pointer parameters in constexpr functions (`const T* p`)
- `ptr == nullptr`, `ptr != nullptr` (null pointer check — always false/true for valid pointers)
- `nullptr == ptr`, `nullptr != ptr` (null pointer check, symmetric form)
- `ptr1 == ptr2`, `ptr1 != ptr2` (pointer equality — compares variable name AND offset)
- `ptr1 < ptr2`, `ptr1 <= ptr2`, `ptr1 > ptr2`, `ptr1 >= ptr2` (pointer relational — both must point into same array)
- `if (ptr)` / `!ptr` / `ptr && x` / `ptr || x` (pointer truthiness — valid pointer is always truthy)

**Still unsupported pointer forms:**
- Pointer-to-member (`obj.*pmf`)

### ✅ Pointer Arithmetic in Constexpr (NEW)

Pointer arithmetic, pointer subscript, address of array elements, pointer difference, and pointer relational comparisons are now supported in constexpr:

```cpp
constexpr int arr[] = {10, 20, 30, 40, 50};

// &arr[i] — address of array element
constexpr const int* p0 = &arr[0];
static_assert(*p0 == 10);             // ✅ Works

constexpr const int* p2 = &arr[2];
static_assert(*p2 == 30);             // ✅ Works

// ptr + n, n + ptr
static_assert(*(p0 + 2) == 30);       // ✅ Works
static_assert(*(2 + p0) == 30);       // ✅ Works

// ptr - n
static_assert(*(p2 - 1) == 20);       // ✅ Works

// ptr[i] (pointer subscript)
static_assert(p0[0] == 10);           // ✅ Works
static_assert(p0[3] == 40);           // ✅ Works

// ptr - ptr (pointer difference)
static_assert(p2 - p0 == 2);          // ✅ Works

// Pointer relational comparisons
static_assert(p0 < p2);               // ✅ Works
static_assert(p0 + 2 == p2);          // ✅ Works

// Pointer arithmetic in constexpr functions
constexpr int sum_via_ptr(const int* p, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        total += p[i];
    }
    return total;
}
static_assert(sum_via_ptr(&arr[0], 5) == 150);  // ✅ Works
```

### ✅ `const char*` and String Literals in Constexpr (NEW)

`const char*` pointers to string literals are supported in constexpr evaluation.
This includes compile-time array subscript, compile-time string-length computation
via `while`/pointer loops, and runtime string usage obtained from constexpr functions.

```cpp
constexpr const char* get_hello() { return "Hello"; }

// Compile-time subscript on constexpr const char*
constexpr const char* hello = get_hello();
static_assert(hello[0] == 'H');   // ✅ Works
static_assert(hello[4] == 'o');   // ✅ Works

// Compile-time string length via constexpr function
constexpr int str_len(const char* s) {
    int len = 0;
    while (s[len] != '\0') ++len;
    return len;
}
static_assert(str_len("Hello") == 5);  // ✅ Works — string literal argument
static_assert(str_len("")      == 0);  // ✅ Works — empty string

// Runtime usage from constexpr function
const char* s = get_hello();  // runtime call
if (s[0] == 'H') { /* ✅ runtime subscript works */ }
int len = str_len(s);         // ✅ runtime call with runtime pointer
```

**How it works:** String literal expressions (e.g., `"Hello"`) are materialised by the
constexpr evaluator as an array of `char` elements (including the null terminator) with
`is_array = true`.  Subscript, pointer-parameter passing, and `while`/`for` loops over
such char arrays all use the existing array/pointer evaluation paths.

**Standard escape sequences** (`\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`) are decoded
correctly when materialising string-literal arrays at compile time.

**Still unsupported in compile-time evaluation:**
- String concatenation at compile-time across different pointer variables (e.g., two
  `const char*` joined with `+`)
- `std::string` / `std::string_view` manipulation at compile time

### ✅ Struct Values Returned from Constexpr Functions (NEW)

Constexpr functions may return aggregate/struct values.  Both compile-time
(`static_assert`) and runtime call sites are supported, including structs whose size
is not a power-of-two machine word (e.g., a 3-byte `Color` struct with three
`unsigned char` fields):

```cpp
struct Point { int x; int y; };
struct Color { unsigned char r; unsigned char g; unsigned char b; };  // 3 bytes

constexpr Point make_point(int x, int y) { return {x, y}; }
constexpr Color make_color(unsigned char r, unsigned char g, unsigned char b) {
    return {r, g, b};
}

// Compile-time
constexpr Point cp = make_point(10, 20);
static_assert(cp.x == 10 && cp.y == 20);  // ✅

// Runtime
Point p  = make_point(3, 4);   // ✅ all members correct
Color c  = make_color(255, 128, 0);  // ✅ all 3 bytes correct (sub-word struct)
```

**Note:** Prior to this fix, sub-word struct returns (e.g., 3-byte structs returned
in a single register) would only propagate the first byte correctly to the caller.
The code-generator now rounds the register store up to the next power-of-two
granularity so every byte of the struct is present before the struct-copy code
reads it field by field.

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
- ✅ Unsigned arithmetic wraps at the declared type's width (e.g. `unsigned int` wraps at 32 bits, `unsigned long long` wraps at 64 bits) when both operands have known exact types
- ✅ Increment/decrement operators (`++` / `--`) correctly wrap at the declared unsigned type's width (e.g. `unsigned int x = UINT_MAX; x++;` wraps to `0`; `unsigned char x = 255; ++x;` wraps to `0`)
- ✅ Shift-count validation for arithmetic-produced left operands: e.g. `(1u + 1u) << 40` is correctly rejected because the result of `1u + 1u` is `unsigned int` (32 bits) and 40 ≥ 32
- ✅ Member array brace-init in constructor initializer lists (e.g., `arr{a, b, c}` for `int arr[3]`) is correctly materialized as an array value in constexpr evaluation
- ✅ C++ aggregate-init zero-fill for partially-specified and single-element array brace-init: `arr{val}` sets `arr[0]=val` and zero-fills the rest; `arr{1,2,3}` for `int arr[5]` zero-fills elements 3 and 4
- ✅ Local variable as array subscript inside constexpr member functions (e.g., `int idx = 1; return arr[idx];`)
- ✅ Basic constexpr pointer dereference: `&named_var` (address-of), `*ptr` (dereference), `ptr->member` (arrow member access), and pointer function parameters (e.g., `const T* p`) in supported shapes
- ✅ Constexpr null pointer checks and pointer comparisons: `ptr == nullptr`, `ptr != nullptr`, `ptr1 == ptr2`, `ptr1 != ptr2`, `!ptr`, `ptr && x`, `ptr || x` (valid constexpr pointer is always non-null/truthy)
- ✅ Constexpr pointer arithmetic: `&arr[i]` (address of array element), `ptr + n`, `n + ptr`, `ptr - n` (pointer arithmetic), `ptr1 - ptr2` (pointer difference), `ptr[i]` (pointer subscript), `ptr < ptr2` / `ptr <= ptr2` / `ptr > ptr2` / `ptr >= ptr2` (pointer relational comparisons); works in both top-level constexpr and inside constexpr function bodies
- ✅ `const char*` and string literals in constexpr: `constexpr const char* s = get_fn();`, `static_assert(s[0] == 'H')`, `str_len("Hello")` using `while (s[n] != '\0')` loop, string literal as function argument; standard escape sequences decoded at compile time
- ✅ Sub-word struct returns from constexpr functions: structs smaller than a machine word (e.g., 3-byte `Color{r,g,b}`) are now correctly propagated in all bytes to both compile-time and runtime call sites

### Medium
- ⚠️ Constexpr free function calls (basic support exists)
- ⚠️ Inferred array size parsing in richer contexts beyond straightforward local array cases (`int arr[] = {1,2,3}`)
- ⚠️ Fold expressions / pack expansions require template instantiation context
- ✅ Range-based for loops over objects with `constexpr begin()`/`end()` member functions are now supported. The iterator methods must return a member array (which the evaluator iterates) or a pointer (`&data[0]` / `&data[N]` style). Template structs with `constexpr begin()`/`end()` are also supported. Nested range-for loops and `break`/`continue` work correctly inside these loops.
- ⚠️ Unsigned wrapping arithmetic: when the declared type cannot be determined (e.g. some template-dependent expressions), the result may fall back to 64-bit storage. Direct identifiers, literals, casts, and most common arithmetic chains all produce correctly-widthed results.
  - Increment/decrement operators (`++` / `--`) now correctly wrap at the declared type's width (e.g. `unsigned int x = UINT_MAX; x++;` wraps to `0`; `unsigned char x = 255; ++x;` wraps to `0`).
- ⚠️ Shift-count validation now uses the promoted left-operand width for direct identifiers, literals, casts, chained shift results, and arithmetic-produced operands (e.g. `(1u + 1u) << 40` is correctly rejected).
  - Some template-dependent or complex intermediate expressions may still fall back to the evaluator's 64-bit storage width when `exact_type` is unavailable.

### Hard
- ⚠️ Complex constructor body statement execution involving complex aliasing or non-trivial call chains (simple assignments, conditionals, loops, and switch now work)
- ⚠️ **Short-circuit `&&` / `||` in top-level `evaluate_binary_operator`** — The bindings-aware evaluation paths (inside constexpr function bodies) already short-circuit correctly (`ConstExprEvaluator_Members.cpp`). However, the top-level path used by `static_assert` and constexpr variable initializers (`ConstExprEvaluator_Core.cpp:evaluate_binary_operator`) eagerly evaluates both sides. Adding short-circuit there causes a regression: `try_evaluate_constant_expression` is used speculatively by the parser to disambiguate `<` (comparison vs template-argument-list). With short-circuit, expressions like `41.5 || non_constexpr_var` succeed (returning `true`), which falsely convinces the heuristic that `<` starts template arguments, breaking parsing of `if (p.a < 41.5 || p.a > 42.5)`. **Fix requires:** coordinating with the template disambiguation logic so that speculative evaluation does not change observable parse behavior when short-circuit is enabled.
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
5. **Pointer arithmetic is now supported** - `&arr[i]`, `ptr + n`, `ptr - n`, `ptr[i]`, `ptr1 - ptr2`, and pointer relational comparisons (`<`, `<=`, `>`, `>=`) all work for constexpr arrays of primitive types; pointer arithmetic in constexpr function bodies (with `&arr[i]` as argument) is also supported; pointer-to-member (`obj.*pmf`) is not yet supported
6. **Use straightforward lambda captures** - the following work best:
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
7. **`const char*` works in constexpr** - constexpr functions can return string literals,
   and subscript/loop operations on `const char*` in both `static_assert` and constexpr
   function bodies work.  `std::string` / `std::string_view` manipulation is not yet
   supported at compile time.
8. **Small structs are safe** - structs smaller than a machine word (e.g., a 3-byte
   `{unsigned char r, g, b}`) can be returned from constexpr functions to both
   compile-time and runtime variables correctly.
9. **Avoid `new` / `delete` and `throw` expressions in constexpr code** for now

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

// Good: Member array brace-init (now supported!)
struct Triplet { int vals[3]; constexpr Triplet(int a, int b, int c) : vals{a, b, c} {} };
constexpr Triplet t(10, 20, 30);
static_assert(t.vals[1] == 20);

// Good: Simple member array access
struct Data { int arr[3]; constexpr Data() : arr{1, 2, 3} {} };
constexpr Data data{};
static_assert(data.arr[1] == 2);

// Good: Basic pointer dereference (now supported!)
constexpr int val = 42;
constexpr const int* ptr = &val;
static_assert(*ptr == 42);

// Good: Arrow member access through constexpr pointer (now supported!)
struct Vec2 { int x, y; constexpr Vec2(int a, int b) : x(a), y(b) {} };
constexpr Vec2 v{3, 4};
constexpr const Vec2* vp = &v;
static_assert(vp->x == 3);

// Good: Null pointer checks and pointer comparisons (now supported!)
constexpr const int* nn_ptr = &val;
static_assert(nn_ptr != nullptr);   // non-null check
static_assert(!(nn_ptr == nullptr)); // same
static_assert(!!nn_ptr);            // truthiness via double-negation
constexpr bool isNull(const int* p) { return p == nullptr; }
static_assert(!isNull(&val));       // false: &val is non-null
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

// Unsupported pointer forms — only pointer-to-member remains unsupported:
// - Pointer-to-member: obj.*pmf  ❌
// Note: pointer arithmetic (ptr + n, ptr - n, ptr[i]), address of array elements
//       (&arr[0]), null pointer checks (ptr == nullptr), pointer equality (ptr1 == ptr2),
//       and pointer truthiness (if (ptr), !ptr, ptr && x) are all now supported.

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

// Note: local variable as array subscript in constexpr member functions NOW WORKS:
// struct Data {
//     int arr[3];
//     constexpr int getSecond() const { int idx = 1; return arr[idx]; }  // ✅ Works
// };
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
- `tests/test_constexpr_struct_runtime_assign_ret0.cpp` - Struct return from constexpr functions (incl. sub-word structs)
- `tests/test_constexpr_const_char_ptr_ret0.cpp` - `const char*` / string-literal support in constexpr
