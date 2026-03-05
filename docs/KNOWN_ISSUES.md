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
