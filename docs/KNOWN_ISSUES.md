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

## Lazy Nested Type Instantiation Ignores Union Flag

### Status: FIXED - `nested_struct.is_union()` now passed correctly

### Description

In `Parser_Templates_Lazy.cpp`, the `instantiateLazyNestedType` function previously
created a `StructTypeInfo` with `is_union` hardcoded to `false`. This was fixed in this
PR by consolidating the `is_union` flag into the `StructTypeInfo` constructor call,
which now correctly reads `nested_struct.is_union()`.

## Template Instantiation Namespace Tracking Incomplete

### Status: TODO - Some secondary instantiation paths still use instantiation-site namespace

### Description

The primary template path and the specialization pattern-match path in
`try_instantiate_class_template` now correctly derive the declaration-site namespace
from `template_name`, `class_decl.name()`, or `pattern_struct.name()`.
However, several secondary instantiation paths still use
`gSymbolTable.get_current_namespace_handle()`, which returns the *instantiation-site*
namespace rather than the *declaration-site* namespace.

### Fixed Paths

- Primary template path (`Parser_Templates_Inst_ClassTemplate.cpp:2596-2609`) — derives
  namespace from `template_name` (if qualified) or `class_decl.name()` (if unqualified).
- Specialization pattern-match path (`Parser_Templates_Inst_ClassTemplate.cpp:721-737`) —
  derives namespace from `template_name` (if qualified) or `pattern_struct.name()` (if
  unqualified), matching the primary template approach.

### Remaining Affected Paths

- `instantiate_full_specialization` (`Parser_Templates_Inst_Substitution.cpp:836,844`)
- `instantiateLazyNestedType` (`Parser_Templates_Lazy.cpp:704,708`)
- Inline nested class in primary template path (`Parser_Templates_Inst_ClassTemplate.cpp:4134`)
- Out-of-line nested class `struct_parsing_context_stack_` push (`Parser_Templates_Inst_ClassTemplate.cpp:4325`)

### Impact

None currently — the `NamespaceHandle` fields on `TypeInfo` and `StructTypeInfo` are
not yet consumed by downstream code (codegen, name mangling). When they are consumed
to fix the same-named-types bug above, these paths will need updating to match the
pattern used in the primary and specialization template instantiation paths.
