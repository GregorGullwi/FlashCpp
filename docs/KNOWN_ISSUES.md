# Known Issues

## Same-Named Types in Different Namespaces

### Status: BUG - Member function calls use wrong type when same name exists in multiple namespaces

### Description

When two namespaces define structs with the same name but different members, the compiler
may confuse member function calls. For example, `physics::Vector::mag()` may be mangled
as `math::Vector::mag()` if `math::Vector` was declared first.

### Root Cause

The `gTypesByName` lookup uses qualified names baked into StringHandle keys, but
member function resolution may resolve the unqualified struct name (e.g., "Vector")
to the first-registered type instead of the namespace-correct one.

### Example Code

```cpp
namespace math {
    struct Vector {
        int x, y;
        int sum() const { return x + y; }
    };
    int compute(Vector v) { return v.sum(); }
}
namespace physics {
    struct Vector {
        int magnitude, direction;
        int mag() const { return magnitude; }
    };
    int compute(Vector v) { return v.mag(); }
}
int main() {
    math::Vector mv{10, 20};
    physics::Vector pv{12, 0};
    return math::compute(mv) + physics::compute(pv); // Expected: 42, Linker error
}
```

### Workaround

Use unique struct names across namespaces until this is fixed.

### Impact

Low - most real-world code uses unique type names. This affects code that relies on
namespace isolation for identically-named types.
