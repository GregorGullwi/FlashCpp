# Known Issues

*All previously known issues have been fixed.*

## Same-Named Types in Different Namespaces

### Status: FIXED

The `lookupTypeInCurrentContext` function now prefers namespace-qualified lookup,
matching C++ name resolution semantics.

## Template Instantiation Namespace Tracking

### Status: FIXED

All template instantiation paths now derive the declaration-site namespace from the
template name or struct declaration name. Additionally:

- `compute_and_set_mangled_name` recovers namespace from the struct's `NamespaceHandle`
  when the current namespace is empty (template instantiation from a different namespace).
- `instantiate_full_specialization` now calls `compute_and_set_mangled_name` on
  member functions (was previously missing).
- Codegen definition, direct call, and indirect call paths all recover namespace from
  `NamespaceHandle` as a fallback when `current_namespace_stack_` is empty.
- `instantiateLazyNestedType` now derives namespace from the parent class's
  `NamespaceHandle` instead of parsing the nested type's qualified name (which
  would treat mangled class names as namespaces).
- Namespace recovery logic is consolidated into `buildNamespacePathFromHandle()`
  in `NamespaceRegistry.h`.

## Constexpr Array Dimensions from Constexpr Variable

### Status: Open (pre-existing)

Using a `constexpr int` variable as an array dimension does not evaluate to the
constant value. For example, `constexpr int N = 42; int arr[N];` produces an
array of size 1 rather than 42. This affects both global and local arrays.

Workaround: use integer literals directly as array dimensions.
