# Template Argument Architecture Audit

**Date:** 2026-05-12

This document describes the current FlashCpp template-argument architecture for
types, non-type values, template-template arguments, class templates, function
templates, constructors, member templates, static members, name lookup, qualified
lookup, ADL, and template argument deduction. It also records the observed
C++20 conformance gaps.

## Executive summary

The current architecture is pragmatic and has accumulated substantial support
for common C++20 template cases, but it is not yet a standard-conforming
template system. The main structural issue is that template argument parsing,
classification, lookup, substitution, deduction, and instantiation are still
spread across parser-owned heuristics, global registries, `TypeInfo` side data,
and late AST rewrites.

C++20 requires template argument interpretation to be driven by declaration
context, name lookup, dependency, the current instantiation, two-phase lookup,
deduction rules, constraints, and overload resolution. FlashCpp often decides
the type/value/template-template category before the target template parameter
is authoritative, then repairs or reinterprets the argument later.

## Current representations

### Template parameters

`TemplateParameterNode` stores a parameter `kind()` as one of:

- `TemplateParameterKind::Type`
- `TemplateParameterKind::NonType`
- `TemplateParameterKind::Template`

It also stores the parameter name, the declared type for non-type parameters,
nested parameters for template-template parameters, defaults, variadic status,
concept constraint metadata, and a registered placeholder `TypeIndex`
(`src/AstNodeTypes_Template.h`).

During parsing, type and template-template parameters are registered as
dependent placeholder `TypeInfo` entries through
`ensureTemplateParameterTypeRegistration(...)` and
`registerTemplateParametersInScope(...)` (`src/Parser_Templates_Params.cpp`).
This makes many dependent names visible to ordinary type lookup machinery.

### Template arguments

`TemplateTypeArg` is the main cross-layer argument carrier
(`src/TemplateTypes.h`). Despite the name, it carries:

- type arguments through `type_index`, cv/ref/pointer/array/member-pointer
  qualifiers, function signatures, and dependency metadata;
- non-type arguments through `is_value`, `value`, `type_index`,
  `dependent_name`, and optional `dependent_expr`;
- template-template arguments through `is_template_template_arg` and
  `template_name_handle`;
- pack-related state through `is_pack`.

`NonTypeValueIdentity` provides a canonical identity carrier for current non-type
arguments, but it is still integral-centric: concrete values are represented by
`int64_t` plus a `TypeIndex`, and dependent values are represented primarily by
name and optional expression metadata.

`TemplateArgIdentity`, `OrderedTemplateInstantiationIdentity`, and
`TemplateInstantiationKey` preserve ordered identity in addition to legacy
grouped type/value/template-template vectors (`src/TemplateRegistry_Types.h`).
This is an important correctness improvement because C++ template identity is
parameter-order sensitive.

### Template environments

`TemplateEnvironment` is a linked environment of `TemplateBinding` entries. Each
binding has a name, a kind, pack status, and one or more `TemplateTypeArg`
values. It supports scalar and pack lookup through `findOne(...)` and
`findPack(...)` (`src/TemplateEnvironment.h`, `src/TemplateEnvironment.cpp`).

Snapshots (`TemplateEnvironmentSnapshot`) are used to preserve outer template
bindings on variables and functions, then reconstruct legacy parameter-name and
argument vectors for older code paths. This gives nested class/member template
instantiation enough outer context for many cases, but it is not yet a complete
semantic instantiation context.

## Current parsing and classification flow

### Template parameter parsing

`parse_template_parameter_list(...)` parses parameters and immediately pushes
their names into the current template parameter state so later defaults can
refer to earlier parameters. `parse_template_parameter(...)` handles:

- type parameters using `typename` / `class`;
- non-type parameters through the normal type/declarator machinery;
- template-template parameters;
- default arguments;
- concept-constrained parameters.

This is close to the right layer for syntactic parameter parsing, but semantic
constraints on parameter forms are still incomplete. The parser registers
template parameters into global-ish type machinery while parsing, which makes
later lookup convenient but blurs C++ scope and two-phase lookup boundaries.

### Explicit template argument parsing

`parse_explicit_template_arguments(...)` is expression-first. It tries to parse
an expression in `ExpressionContext::TemplateTypeArg`, classifies literals and
constant expressions as non-type arguments, accepts selected dependent
compile-time expressions, and falls back to `parse_type_specifier(...)` for
types (`src/Parser_Templates_Params.cpp`).

For simple identifiers it uses `classifySimpleTemplateArgName(...)`, which
checks:

- current template parameter metadata;
- current substitution records;
- symbol-table lookup;
- variable templates;
- enclosing class static data members;
- alias templates, class templates, and known types.

This heuristic allows many ambiguous cases to work, including static member
values in template bodies, but it is not the C++20 model. C++20 classifies an
argument against the corresponding template parameter after lookup and template
parameter matching. A token sequence such as `X` or `N` can only be interpreted
correctly with the target parameter kind, the point of lookup, dependency state,
and possible `typename` / `template` disambiguators.

## Current instantiation architecture

### Registry and instantiation keys

`TemplateRegistry` stores class/function templates, alias templates, variable
templates, deduction guides, partial specializations, instantiation keys, and
outer bindings (`src/TemplateRegistry_Registry.h`). It registers both
unqualified and some qualified names for convenience.

The registry lookup model is name-handle based. It does not yet represent
declaration contexts as first-class lookup scopes equivalent to the C++ model.
Consequently, namespaces, class scopes, injected-class-names, using-declarations,
using-directives, hidden friends, and overload sets are partly reconstructed by
call-site logic instead of flowing through one authoritative lookup result.

### Class templates

Class template instantiation in
`src/Parser_Templates_Inst_ClassTemplate.cpp` builds substitution maps, expands
packs, substitutes base classes, members, member functions, aliases, static
members, and deferred bodies. It also materializes static member initializers
and retries normalization after deferred member bodies.

This path owns a large amount of semantic work. It is responsible for converting
dependent placeholder facts into concrete `TypeInfo`, copying and rebinding
member AST, rebuilding qualified member names, and invoking constexpr
evaluation for static initializers.

### Function templates and member function templates

Function template instantiation and deduction live mainly in
`src/Parser_Templates_Inst_Deduction.cpp`. Explicit template arguments are
merged with deduced call-argument information, defaults, concept checks, trailing
return SFINAE reparsing, instantiation-key lookup, and overload candidate
probing.

Member function templates use a similar but separate path in
`src/Parser_Templates_Inst_MemberFunc.cpp`. That path deduces from member call
argument types, handles outer class template bindings, and reconstructs owner
names when the concrete instantiated owner name differs from the source
template owner.

### Alias templates and variable templates

Alias templates have a deferred-target representation in `TemplateAliasNode`.
Alias materialization reparses or substitutes stored target argument nodes and
may classify deferred qualified identifiers against a target template parameter
(`src/Parser_Templates_Inst_Substitution.cpp`,
`src/Parser_Templates_Variable.cpp`).

Variable templates are registered separately from class/function/alias
templates. Their lookup participates in simple argument classification as a
value-like name.

### Static members

Static data members are represented in class AST and `StructTypeInfo`.
Class-template instantiation can early-normalize static member initializers by:

- rebinding function calls in the initializer AST;
- instantiating deferred static initializer call targets;
- substituting template parameters;
- evaluating the initializer as a constant expression;
- storing normalized bytes when possible.

This supports many trait-style patterns, but the timing is implementation
driven. C++20 requires static member declarations, definitions, initialization,
constant evaluation, odr-use, and template instantiation to observe precise
lookup and point-of-instantiation rules.

## Name lookup architecture

### Ordinary and qualified lookup

The symbol table supports scoped symbols, namespace symbols, using directives,
using declarations, namespace aliases, and overload vectors
(`src/SymbolTable.h`). Qualified lookup support exists in parser expression and
type paths, and templates are often registered under both unqualified and
qualified names.

However, template lookup is not unified with ordinary/qualified lookup. Some
template operations call `gTemplateRegistry.lookupTemplate(...)`,
`lookup_alias_template(...)`, `lookupVariableTemplate(...)`, or
`isClassTemplate(...)` directly by name. This bypasses a single declaration
lookup result and can select a template by spelling rather than by standard
scope rules.

### ADL

ADL exists for ordinary calls and operators. The symbol table tracks
namespace-scoped symbols and ADL-only hidden friends, and expression parsing has
call-resolution paths that combine ordinary lookup, ADL, overload resolution,
and template fallback (`src/SymbolTable.h`, `src/Parser_Expr_PrimaryExpr.cpp`,
`src/OverloadResolution.h`).

This is a strong base for call expressions, but template argument parsing,
template-id classification, alias materialization, member template lookup, and
static-member initializer rebinding do not consistently consume the same
ordinary-plus-ADL candidate pipeline. C++20 ADL affects function calls, including
function template candidates, and must interact with two-phase lookup for
dependent calls.

## Template argument deduction architecture

Current deduction is parser-owned. It builds maps from template parameter names
to `TemplateTypeArg` values by matching function parameter types against call
argument types. It has support for:

- explicit function template arguments;
- variadic packs and co-packs;
- nested template argument extraction;
- array-bound NTTP deduction;
- defaults;
- constraints;
- trailing return SFINAE reparse;
- SFINAE candidate collection in selected paths.

The architecture is not yet the C++20 deduction model. Important gaps include:

- incomplete non-deduced context handling;
- incomplete forwarding-reference and cv/ref transformation rules;
- incomplete overload-set and function-pointer deduction;
- incomplete template-template parameter matching;
- incomplete partial ordering of function templates;
- constraints are checked, but not yet modeled as normalized atomic constraints
  with subsumption;
- CTAD and deduction guides are represented, but not as a complete C++20
  overload-set construction and selection pipeline;
- deduction and overload viability are still interleaved with instantiation and
  registry lookup rather than being separate semantic phases.

## Standard-conformance assessment

### Mostly aligned areas

- Template parameters have an explicit type / non-type / template-template kind.
- Template argument identity now preserves source order.
- Type arguments carry cv/ref/pointer/array/function-signature metadata.
- Dependent non-type expressions can preserve AST for later reevaluation.
- Template environments and snapshots preserve some outer-template state.
- ADL and hidden-friend infrastructure exists for ordinary calls.
- Lazy materialization has moved toward sema-owned ODR-use handling in recent
  work.

### Non-conforming or structurally risky areas

#### 1. Template argument kind is often heuristic

C++20 template argument interpretation is parameter-context sensitive. FlashCpp
often classifies arguments before the target parameter is available or before
lookup is authoritative. This affects ambiguous simple identifiers, qualified
ids, dependent expressions, aliases, variable templates, and static members.

#### 2. Non-type template arguments are too narrow

C++20 NTTPs include integral values, enumeration values, pointers, references,
member pointers, `nullptr`, floating-point values, and structural class-type
values subject to the C++20 structural-type rules. FlashCpp's concrete
`NonTypeValueIdentity` is centered on `int64_t`, with partial type identity.
That is not sufficient for standard NTTP equivalence or mangling.

#### 3. Dependent names are represented as strings/placeholders

Many dependent facts are stored as `dependent_name`, placeholder `TypeInfo`, or
optional AST. C++20 requires a richer distinction among unknown specialization,
member of current instantiation, dependent base lookup, dependent qualified
name, dependent template-id, and dependent expression. String identity is not
enough to implement these rules reliably.

#### 4. Two-phase lookup is incomplete

The current system reparses and substitutes using current parser state, global
registries, and symbol tables. It does not yet have a complete point-of-definition
ordinary lookup snapshot plus point-of-instantiation dependent lookup model.
This risks both accepting ill-formed code and rejecting valid dependent code.

#### 5. Qualified lookup is not one authoritative semantic service

Qualified lookup is split across parser type parsing, expression parsing,
registry lookup, string-based qualified names, namespace registry helpers, and
member-template reconstruction. C++20 qualified lookup has precise behavior for
class scopes, base classes, dependent bases, namespaces, injected-class-names,
using-declarations, and access. These need one semantic lookup engine.

#### 6. ADL is not uniformly integrated with templates

Ordinary call parsing has ADL support, but template fallback, template function
candidate lookup, dependent calls, hidden friends, and static initializer
rebinding do not all flow through one call-resolution service. C++20 requires
function template candidates found by ADL to participate in overload resolution
and two-phase lookup.

#### 7. Deduction and overload resolution are interleaved with instantiation

C++20 separates candidate construction, template argument deduction, constraint
checking, viability, partial ordering, overload resolution, substitution, and
instantiation. FlashCpp currently mixes several of these steps while searching
the registry and materializing AST.

#### 8. Static member handling is too eager and repair-oriented

Static member initializers are rebound, substituted, instantiated, and
constant-evaluated inside class-template instantiation. This can work for traits,
but C++20 requires precise instantiation timing, constant-evaluation context,
definition availability, and odr-use behavior.

#### 9. Current-instantiation identity is incomplete

Outer bindings and owner reconstruction support many member-template cases, but
the compiler does not yet maintain an authoritative current-instantiation model
for all qualified names, nested classes, dependent bases, injected-class-names,
and member access.

## Conclusion

The current architecture is useful and increasingly capable, but it is not
standard compliant as a whole. The highest-priority architectural issue is not a
single parser bug; it is the absence of one semantic template system that owns:

1. declaration lookup and dependency classification;
2. template parameter and argument matching;
3. two-phase lookup;
4. deduction and constraints;
5. substitution and point-of-instantiation materialization;
6. constant evaluation under an instantiated semantic context.
