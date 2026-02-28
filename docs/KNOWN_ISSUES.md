# Known Issues

## Template Operator Overloading (Codegen)

**Symptoms**: When a template struct (e.g. `template<typename T> struct W { T value; }`) has
operator overloads (such as `operator+=`, `operator==`, `operator<`), the generated code
may produce incorrect results at runtime:

- **Compound assignment operators** (`+=`, `-=`, `*=`, `/=`, etc.) on template-instantiated
  structs produce wrong values. For example, `W<int> a{100}; W<int> b{5}; a += b;` gives
  `a.value == 112` instead of `105`.
- **Comparison operators** (`==`, `!=`, `<`) on template-instantiated structs always return
  the wrong result.
- **Operators returning struct by value** (e.g. `operator+` returning a new struct) from
  template-instantiated structs produce incorrect results.

Non-template struct operator overloading works correctly for all of the above.

The issue is likely in the codegen path for template-instantiated member function calls
where struct member offsets or `this` pointer handling differs from non-template structs.

## Prefix Increment/Decrement on Multi-Member Structs

**Symptoms**: When `operator++()` (prefix increment) modifies multiple members of a struct
through `*this`, only the first member is correctly updated. For example:
```cpp
struct Vec2 {
    int x, y;
    Vec2& operator++() { ++x; ++y; return *this; }
};
Vec2 v{5, 5};
++v; // v.x == 6 (correct), v.y == 5 (incorrect, should be 6)
```

Single-member structs work correctly with prefix increment/decrement.
