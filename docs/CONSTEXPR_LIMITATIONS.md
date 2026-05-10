# Constexpr Evaluation Limitations

**Standard boundary:** C++20 constexpr. `try`/`catch` inside constant evaluation is not a support target.

## Overview

FlashCpp implements a custom constexpr evaluator used for `static_assert`, template non-type arguments, and constant-expression variable initializers. The evaluator walks AST nodes with a binding map that tracks local variable state, object member bindings, and capture state for lambdas and callable objects.

## Supported Features ✅

### Primitives and arithmetic
- All primitive types (`bool`, `char`, `signed/unsigned char/short/int/long/long long`, `float`, `double`, `long double`) as constexpr variables and in arithmetic/comparisons.
- Mixed-type arithmetic follows C++ usual arithmetic conversions.
- Unsigned wrapping at the declared type's width (`unsigned int`, `unsigned char`, etc.) for arithmetic and all compound-assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`), including `++`/`--`.
- Shift-count validation against the promoted left-operand width.
- C-style casts and `static_cast` / cv-only `const_cast` inside constexpr function bodies.
- `sizeof(expr)` and `alignof(expr)` for common expression operands.
- `noexcept(expr)` and `offsetof(T, member)` (including simple nested forms).
- Comma operator (`(side_effect, result)`).

### Struct construction and member access
- Aggregate initialization (brace-init and C++20 paren-init), including C++20 designated initializers (`{.x=1, .y=2}`).
- User-defined constructors: member-initializer list, constructor body assignments, conditionals, loops, switch, early return.
- Default member initializers.
- Nested member access (`obj.inner.value`) on both aggregate and constructor-initialized locals.
- Local struct member dot-assignment and compound-assignment (`p.a += 5`).
- Void member functions that mutate local struct state, including repeated calls.
- `*this` dereference inside constexpr member function bodies.
- Ternary expressions returning struct types.
- Sub-word struct returns (e.g., 3-byte `Color{r,g,b}`) propagated correctly.
- C++20 rule: types with user-declared constructors are not aggregates; aggregate initialization rejects them.
- Inheritance: `Derived : Base` with constructor chaining via member-initializer lists.

### Functions, lambdas, and callables
- Multi-statement constexpr free functions with local variables, `if`/`else`, `for`, `while`, `switch`, `break`, `continue`, and `return`.
- Overload resolution reuses sema-resolved call target for both free functions and callable objects.
- Constexpr lambdas: explicit captures (`[x]`, `[&x]`), default captures (`[=]`, `[&]`), init-captures, `[this]`, `[*this]`.
- Void lambdas (implicit or explicit `-> void`) with by-reference captures that mutate outer locals, including early `return;` paths.
- Mutable closure-local state across repeated lambda calls.
- Returned lambda closure objects from constexpr helper functions with repeated calls.
- Nested lambdas over enclosing captured/member state.
- `operator()` called on locally-declared struct variables (aggregate and non-aggregate).
- Callable objects (functors) with `operator()` overloads; trailing default arguments.
- `const char*` / string literals: subscript, loops, `str_len`-style traversal.

### Arrays and pointers
- Explicit-size and inferred-size (`int arr[] = {1,2,3}`) local and global arrays.
- Multi-dimensional arrays (`int m[M][N]`) with nested-brace init, element reads/writes, brace-elision.
- Member arrays via brace-init in constructor initializer lists.
- Range-based for over local arrays, arrays with struct element types, and objects with `constexpr begin()`/`end()`.
- Pointer dereference (`*ptr`), pointer arithmetic (`ptr+n`, `ptr-n`, `ptr[i]`, `ptr1-ptr2`), `&arr[i]`.
- Pointer comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`; null checks (`ptr == nullptr`); truthiness (`if (ptr)`, `!ptr`).
- Arrow member access (`ptr->member`) through constexpr and heap-allocated pointers.
- Pointer-to-member: `&Type::member`, `obj.*pm`, `ptr->*pm`.
- Dereference assignment `*p = value` and arrow member write `p->member = value`.

### Dynamic allocation (`new`/`delete`)
- Scalar `new T(args)` / `new T()` / `new T{}`, array `new T[n]`, `delete`, `delete[]`.
- Struct/class allocation with constructor or aggregate arguments.
- Arrow member read/write on heap structs; subscript assignment on heap arrays.
- Value-initialization (`new T()`, `new T{}`) zero-fills members without default initializers.
- Bare `new T` / `new T[n]` (default-initialization) rejects reads of indeterminate scalars.
- Leak detection: all heap allocations must be freed before the constant expression returns.
- Use-after-free through arrow access produces a clear diagnostic.

### Diagnostics
- Evaluated `throw` produces a "not a constant expression" diagnostic; untaken branches are skipped.
- Indeterminate reads (`int x; return x;`, `Pair p; return p.first;`) are rejected.
- C++20 designated-initializer diagnostics: mixed positional/designated and out-of-order designators rejected.
- `new` on non-aggregate type without a default constructor rejected with a clear error.

---

## Partial Support ⚠️

### Nested lambdas with shared by-reference state

Lambdas that capture another lambda *by reference* and then call it — where the inner lambda also writes to a shared outer variable — lose the write-back on the outer hop.

```cpp
constexpr int f() {
    int x = 0;
    auto inc = [&x]() { x++; };        // inner: writes x via capture
    auto twice = [&inc]() { inc(); inc(); };  // outer: captures inc by ref
    twice();
    return x;  // ❌ evaluates to 0, not 2
}
```

**Root cause:** When `twice` is evaluated, `inc` is a bound lambda value in `twice`'s capture bindings. After calling `inc()`, the write-back updates `inc`'s *copy* of `x` inside `twice`'s binding map, but that copy is not propagated back to the *outer* binding map that holds the original `x`. The evaluator's single-level by-reference writeback (`mutable_outer_bindings`) does not chase indirect capture chains.

**What is needed:** After each lambda call that wrote to captured references, the evaluator must walk back through any intermediate lambda-capture indirection layers and propagate the mutations outward until the original binding is reached.

---

### Subscript through a struct member `const char*`

`operator[]` on a struct whose implementation subscripts a `const char*` data member fails.

```cpp
struct Str {
    const char* d;
    constexpr char operator[](int i) const { return d[i]; }
};
constexpr Str s{"hi"};
static_assert(s[0] == 'h');  // ❌ "Array subscript on unsupported expression type"
```

**Root cause:** Inside `operator[]`, `d` is resolved as an `EvalResult` holding a string-literal pointer (via the bound `const char*` member). The subscript evaluator (`try_evaluate_bound_array_subscript` and the top-level subscript path) accepts `const char*` results from *named constexpr variables*, but not from a member-binding dereference — the path that extracts `d`'s value from `object_member_bindings` does not set the `pointer_to_var` / origin metadata required by the string-subscript fast-path.

**What is needed:** After resolving `d` from `object_member_bindings` to a string-literal result, propagate the `pointer_to_var.origin_var_name` so the subscript path can find the string data. Alternatively, teach the subscript handler to operate directly on a string-literal `EvalResult` regardless of how it was obtained.

---

### Fold expressions and pack expansions in constexpr

Fold expressions (`(vs + ...)`) only work when the pack is already expanded by the template instantiator before constexpr evaluation begins.

```cpp
template<typename... Ts>
constexpr int sum(Ts... vs) { return (vs + ...); }
// Works only after full template instantiation; may fail in un-instantiated contexts.
```

**Root cause:** The constexpr evaluator receives an already-parsed AST. If the fold expression node still contains an unexpanded pack (because the template was not yet instantiated with concrete arguments), the evaluator has no mechanism to iterate over the pack elements — it sees a single `FoldExprNode` with no concrete operands.

**What is needed:** The evaluator must be able to trigger on-demand pack expansion for fold expressions, or the template instantiator must fully expand packs before the expression reaches constexpr evaluation. This is already the path taken for fully-instantiated templates; the gap is un-instantiated or partially-instantiated contexts.

---

### Unsigned wrapping in template-dependent expressions

When the declared type of an intermediate result cannot be statically determined (e.g., in a template-dependent arithmetic chain), the result falls back to 64-bit storage and unsigned wrapping is not applied.

```cpp
template<typename T>
constexpr T wrap_add(T a, T b) { return a + b; }
// May not wrap at T's width for unsigned T when the declared type is opaque.
```

**What is needed:** The evaluator must propagate `exact_type` through all arithmetic operations so that width truncation is applied even for template-dependent intermediate values.

---

### Inferred array size in richer contexts

`int arr[] = {1,2,3}` in straightforward local and global constexpr contexts works. The following still fail:

```cpp
// Inferred-size array returned from a constexpr function
constexpr auto make_arr() { int arr[] = {1,2,3}; return arr; }  // ❌

// Inferred-size array as a struct member (requires sizeof deduction at class layout time)
struct S { int data[]; };  // ❌ (flexible array member — also ill-formed in C++)
```

**Root cause:** The evaluator relies on the array having a known element count in `EvalResult::array_elements`. When the count is not materialized before evaluation (e.g., inside an uninstantiated template or for a function return type), the array remains opaque.

---

## Not Supported ❌

### Indirect mutation through a captured lambda reference

A lambda `B` that captures lambda `A` by reference and calls `A` does not propagate `A`'s mutations to the variables `A` closed over.

```cpp
constexpr int f() {
    int x = 0;
    auto inc = [&x]() { x++; };
    auto twice = [&inc]() { inc(); inc(); };
    twice();
    return x;  // ❌ 0 instead of 2
}
```

Same root cause as the partial-support case above; listed here because it is a common pattern and there is no workaround other than inlining the body.

---

### `std::initializer_list` in constexpr

```cpp
constexpr int sum(std::initializer_list<int> vals) {
    int s = 0;
    for (int v : vals) s += v;
    return s;
}
static_assert(sum({1, 2, 3}) == 6);  // ❌ "Expression type not supported in constant expressions"
```

**Root cause:** `std::initializer_list<T>` is a library type backed by a compiler-synthesized backing array and two pointers (`begin`/`end`). FlashCpp's constexpr evaluator does not model this synthetic object. The brace-argument list `{1,2,3}` at a call site that expects `std::initializer_list` is parsed as an `InitializerListNode`, but the evaluator does not translate it into the `begin`/`end` pointer pair the library type exposes.

**What is needed:** Special-case `std::initializer_list<T>` in the evaluator: when a function parameter has this type and the argument is an `InitializerListNode`, synthesize an internal array `EvalResult` and make `begin()`/`end()` return suitable pointer results over it.

---

### `try`/`catch` in constexpr

C++20 permits `try`/`catch` blocks inside constexpr functions (they just cannot be entered during constant evaluation). FlashCpp does not currently parse `try` blocks inside constexpr function bodies, so any constexpr function containing a `try` block will fail to compile.

**What is needed:** Parser support for `try`/`catch` inside constexpr bodies; the evaluator then simply never enters the handler during constant evaluation, so no evaluator changes are needed.

---

### Complex member-initialization chains (delegating constructors, calling non-constexpr helpers)

```cpp
struct S {
    int x;
    constexpr S() : S(0) {}        // ❌ delegating constructor not evaluated
    constexpr S(int v) : x(v) {}
};
constexpr S s;
static_assert(s.x == 0);
```

**Root cause:** The evaluator's `try_materialize_struct_from_ctor_args` path does not detect or recurse into delegating constructors. When the selected constructor's initializer list contains another constructor call (`:S(0)`), the path does not recognize this as a delegation and falls through without producing member bindings.

**What is needed:** After finding a matching constructor, inspect its initializer list for a delegating constructor call (initializer target is the same class name). If found, recursively invoke the delegated constructor to obtain the initial member state, then apply any additional initializer-list entries on top.

---

### Virtual dispatch in constexpr

```cpp
struct Base { virtual constexpr int value() const { return 0; } };
struct Derived : Base { int x; constexpr Derived(int v) : x(v) {}
    constexpr int value() const override { return x; } };
constexpr Derived d(42);
static_assert(d.value() == 42);  // ✅ direct call on concrete type — works
// But through a Base* / Base& pointer, virtual dispatch is not resolved.
```

Virtual dispatch through a base-class pointer or reference is not implemented in the evaluator. C++20 does require it to work; FlashCpp does not track vtable information in `EvalResult`.

**What is needed:** The member-function lookup must consult the *dynamic type* (stored in `object_type_index`) when the static type is a base class, and route to the most-derived override.

---

## Implementation Architecture

The evaluator lives in `src/ConstExprEvaluator*.cpp` (split into `_Core`, `_Members`, and header files). Central types:

- **`EvalResult`** — carries a scalar value, optional `object_member_bindings` (for struct objects), `array_elements` (for arrays), `callable_lambda` / `callable_var_decl` / `callable_bindings` (for lambda/functor callables), and `pointer_to_var` (for pointers). All evaluation results pass through this type; copies are value-semantic and expensive for large objects.
- **`EvaluationContext`** — holds the current binding maps (`local_bindings`, `global_bindings`), struct info for member function evaluation, recursion depth, and flags (`is_speculative`).
- **`evaluate_block_with_bindings`** — evaluates a sequence of statements, returning either a successful value (return statement) or a sentinel error. Loops, conditionals, switch, break/continue all terminate the block early via specific error sentinel strings (`kBreakExecuted`, `kContinueExecuted`, etc.).

Key design constraint: the evaluator is a tree-walk interpreter with no heap or mutable global state between evaluations. Each constexpr call starts fresh from the AST and binding maps.

### Architectural debt: `EvalResult` copy pressure

`EvalResult` is value-semantic and copied frequently. For large objects (deep member-binding maps, large arrays, lambda closures with many captures) this becomes expensive. A tracked follow-up task is to split heavy recursive state into arena-managed payload objects while keeping scalar results inline.

---

## Recommendations

### For Users

1. **Prefer member-initializer lists** over complex constructor-body chains for most reliable constexpr support.
2. **Void lambdas with `[&]` captures work** — accumulator and mutating-callback patterns are fully supported.
3. **Avoid indirect lambda capture chains** — `[&inc]() { inc(); }` where `inc` itself captures by-ref does not propagate writes back. Inline the body instead.
4. **`constexpr` lambdas and returned lambda values are supported**, including `constexpr auto fn = make_fn(); static_assert(fn(...));`.
5. **Dynamic allocation works** — `new`/`delete` in constexpr follow C++20 rules; all allocations must be freed before the constant expression returns.
6. **`const char*` string operations work** — subscript, `while (*s != '\0')` traversal, and string-literal return values are all supported.
7. **Avoid `std::initializer_list` parameters** — use explicit array parameters or variadic templates instead.

### For Contributors

The most impactful next improvements in rough priority order:

1. **Propagate mutations through indirect lambda-capture chains** — fix `evaluate_lambda_call` to walk capture-of-capture chains after each call and propagate writes all the way to the original binding.
2. **`const char*` member subscript** — after resolving a `const char*` member binding, propagate `pointer_to_var.origin_var_name` so the subscript fast-path can find the string data.
3. **Delegating constructors** — detect delegation in the initializer list and recurse before applying any remaining initializer entries.
4. **`std::initializer_list`** — synthesize an internal array from the brace-argument list when the parameter type is `std::initializer_list<T>`.
5. **Virtual dispatch** — use `object_type_index` as the dynamic type when looking up member functions through a base-class static type.

---

## See Also

- `src/ConstExprEvaluator.h` / `src/ConstExprEvaluator_Core.cpp` / `src/ConstExprEvaluator_Members.cpp` — evaluator implementation
- `tests/test_constexpr_comprehensive.cpp` — general working constexpr patterns
- `tests/test_constexpr_structs.cpp` — struct construction and member access
- `tests/test_constexpr_member_func.cpp` — member function constexpr tests
- `tests/test_constexpr_struct_runtime_assign_ret0.cpp` — struct return from constexpr functions (incl. sub-word structs)
- `tests/test_constexpr_const_char_ptr_ret0.cpp` — `const char*` / string-literal support
- `tests/test_constexpr_new_delete_ret0.cpp` — constexpr `new`/`delete`
- `tests/test_constexpr_new_scalar_brace_narrowing_fail.cpp` — scalar `new T{arg}` rejects narrowing
- `tests/test_constexpr_new_scalar_paren_narrowing_ret0.cpp` — scalar `new T(arg)` uses normal conversions
- `tests/test_constexpr_new_leak_fail.cpp` — constexpr heap leak detection (expected failure)
- `tests/test_constexpr_new_default_init_scalar_fail.cpp` / `_array_fail.cpp` / `_aggregate_fail.cpp` — bare `new T` indeterminate-read rejection
- `tests/test_constexpr_new_value_init_aggregate_ret0.cpp` — value-init zero-fills correctly
- `tests/test_constexpr_short_circuit_ret0.cpp` — short-circuit `&&`/`||`
- `tests/test_constexpr_designated_init_member_ret42.cpp` — C++20 designated initializer support
- `tests/test_constexpr_designated_init_mixed_fail.cpp` / `_order_fail.cpp` — designated initializer diagnostics
- `tests/test_constexpr_bitwise_compound_assign_ret0.cpp` — bitwise compound assignments
- `tests/test_constexpr_struct_ctor_in_body_ret0.cpp` — struct constructor calls inside constexpr functions
- `tests/test_constexpr_reference_alias_ctor_body_ret0.cpp` — reference-alias mutation in constexpr
- `tests/test_constexpr_local_member_assign_ret0.cpp` — local struct member dot-assignment
- `tests/test_constexpr_this_deref_ret0.cpp` — `*this` dereference in constexpr member functions
- `tests/test_constexpr_multidim_array_ret0.cpp` — multi-dimensional array constexpr support
- `tests/test_constexpr_ternary_struct_ret0.cpp` — ternary returning struct types
- `tests/test_constexpr_nested_member_ctor_local_ret0.cpp` — nested member access on local constructor-initialized objects
- `tests/test_constexpr_global_paren_init_ret0.cpp` — global struct paren-init
- `tests/test_constexpr_global_float_struct_ret0.cpp` — global struct paren-init with float/double members
- `tests/test_constexpr_void_lambda_ref_capture_ret0.cpp` — void constexpr lambdas with by-reference capture mutations
- `tests/test_constexpr_local_callable_operator_ret0.cpp` — `operator()` on locally-declared struct variables
- `tests/test_constexpr_returned_lambda_global_ret0.cpp` — returned lambda values stored in global `constexpr auto` variables
