# Known Issues

## Same-Named Types in Different Namespaces

### Status: FIXED - `lookupTypeInCurrentContext` now prefers namespace-qualified lookup

### Description

When two namespaces define structs with the same name but different members, the compiler
previously confused member function calls. For example, `physics::Vector::mag()` was mangled
as `math::Vector::mag()` if `math::Vector` was declared first.

### Root Cause

The `lookupTypeInCurrentContext` function performed unqualified name lookup first,
which found a short-name alias registered by the first namespace. This was fixed by
reordering the lookup to try namespace-qualified names first (matching C++ name
resolution semantics), then falling back to unqualified lookup.

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
    return math::compute(mv) + physics::compute(pv); // Returns 42
}
```

## Lazy Nested Type Instantiation Ignores Union Flag

### Status: FIXED - `nested_struct.is_union()` now passed correctly

### Description

In `Parser_Templates_Lazy.cpp`, the `instantiateLazyNestedType` function previously
created a `StructTypeInfo` with `is_union` hardcoded to `false`. This was fixed in this
PR by consolidating the `is_union` flag into the `StructTypeInfo` constructor call,
which now correctly reads `nested_struct.is_union()`.

## Template Instantiation Namespace Tracking

### Status: FIXED - All paths now derive declaration-site namespace

### Description

Template instantiation paths now correctly derive the declaration-site namespace
from the template name or struct declaration name, instead of using the
instantiation-site namespace from `gSymbolTable.get_current_namespace_handle()`.

### Fixed Paths

- Primary template path (`Parser_Templates_Inst_ClassTemplate.cpp`) — derives
  namespace from `template_name` (if qualified) or `class_decl.name()` (if unqualified).
- Specialization pattern-match path (`Parser_Templates_Inst_ClassTemplate.cpp`) —
  derives namespace from `template_name` (if qualified) or `pattern_struct.name()`.
- `instantiate_full_specialization` (`Parser_Templates_Inst_Substitution.cpp`) —
  derives namespace from `template_name` (if qualified) or `spec_struct.name()`.
- `instantiateLazyNestedType` (`Parser_Templates_Lazy.cpp`) — derives namespace
  from the qualified name of the nested type.
- Inline nested class in primary template path (`Parser_Templates_Inst_ClassTemplate.cpp`)
  — uses `decl_ns` from the enclosing template instantiation.
- Out-of-line nested class `struct_parsing_context_stack_` push
  (`Parser_Templates_Inst_ClassTemplate.cpp`) — uses `decl_ns` from the enclosing
  template instantiation.
