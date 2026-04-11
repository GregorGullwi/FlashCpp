# Known Issues

## EBO / `[[no_unique_address]]` — PR #1184

### Investigations

#### `[[no_unique_address]]` only handles empty types, not tail-padding reuse

`src/AstNodeTypes_DeclNodes.h:185-194` — The current implementation only applies
the zero-size optimization for `isEmptyLayoutLike()` types. Per C++20
\[dcl.attr.nouniqueaddr\], the attribute also permits reusing tail padding of
non-empty members. This is a standards-compliant simplification (the attribute is
permissive, not mandatory) but means `struct S { [[no_unique_address]] NonEmpty a; char c; }`
will not benefit from potential tail padding optimization.

## Namespace-qualified out-of-line template member registration — PR #1219

### Investigations

#### `registerSpecialization` path not updated for namespace qualification

`src/Parser_Templates_MemberOutOfLine.cpp:768-779` — When a member function
has its own template arguments (the `is_specialization` branch), the
`qualified_name` used for `registerSpecialization` is built from the bare
`class_name + "::" + function_name` without incorporating the current
namespace.  This means namespace-scoped member function *template*
specializations (e.g., `template<> template<> void ns::Container<int>::convert<double>(...)`)
would still be registered under an unqualified key.  A `QualifiedIdentifier`-based
`registerSpecialization` overload already exists at
`src/TemplateRegistry_Registry.h:656` and could be used here.

#### `registerOutOfLineNestedClass` QualifiedIdentifier overload is dead code

`src/TemplateRegistry_Registry.h:488-492` — The PR adds a `QualifiedIdentifier`
overload for `registerOutOfLineNestedClass` for consistency, but there are
currently **no callers** of any `registerOutOfLineNestedClass` overload in the
codebase.  If namespace-scoped out-of-line nested class definitions are added
in the future, the callers should use the `QualifiedIdentifier` overload to
get dual-registration.

#### Out-of-namespace definitions not covered by the fix

`src/Parser_Templates_MemberOutOfLine.cpp:12-16` — The
`makeQualifiedClassIdentifier` lambda uses `gSymbolTable.get_current_namespace_handle()`
to determine the namespace context.  This correctly handles definitions
*inside* the namespace block (e.g., `namespace ns { template<> int Box<int>::value() ... }`).
However, for definitions *outside* the namespace block using a fully-qualified
class name (e.g., `template<typename T> int ns::Box<T>::value() const { ... }`
at global scope), the parser consumes the namespace components without
capturing them, so `class_name` is the bare name `"Box"` and the current
namespace is global — the qualified name `"ns::Box"` is never registered.
This is the same limitation as the pre-PR code and not a regression, but the
fix is incomplete for this pattern.
