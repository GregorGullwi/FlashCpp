# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12

This document describes how FlashCpp's template argument architecture can move
toward C++20 conformance. It is intentionally architectural: it identifies the
compiler layers that should own each responsibility and the order in which to
separate them.

## Target architecture

### 1. Authoritative declaration and lookup model

Create one semantic lookup service used by parser, sema, constexpr evaluation,
template instantiation, and codegen-facing normalization.

The lookup result should preserve:

- declaration identity, not only spelling;
- declaring scope and namespace/class context;
- whether the result is a type, value, namespace, template, overload set, or
  dependent result;
- access and using-declaration provenance;
- whether ordinary lookup, qualified lookup, member lookup, or ADL produced it;
- point-of-definition versus point-of-instantiation timing.

Template registries should become indexes over declarations, not independent
name lookup mechanisms. `TemplateRegistry` can still cache templates and
instantiations, but candidate discovery should start from semantic lookup.

### 2. First-class dependent name model

Replace string-like dependent placeholders with semantic dependent entities.

The compiler should distinguish:

- dependent type name;
- dependent value expression;
- dependent template name;
- member of current instantiation;
- member of unknown specialization;
- dependent qualified name;
- dependent base lookup;
- dependent template-id;
- unresolved overload set dependent on argument types.

This allows `typename`, `template`, current-instantiation lookup, dependent
bases, and unknown specializations to be implemented according to C++20
`[temp.dep]`, `[temp.res]`, and `[basic.lookup]` instead of by reparsing and
fallback classification.

### 3. Parameter-context-driven template argument classification

Template arguments should be parsed into syntax first, then semantically
classified against the corresponding template parameter.

The target flow should be:

1. parse a template-id into argument syntax nodes without forcing type/value
   classification;
2. perform template name lookup and select the target template declaration or
   overload set;
3. match arguments to parameters, including packs and defaults;
4. classify each argument by parameter kind:
   - type parameter: parse/resolve as type-id;
   - non-type parameter: convert constant expression to the parameter type;
   - template-template parameter: resolve as template-name and match parameter
     list;
5. diagnose mismatches at this layer.

This removes most simple-identifier heuristics from
`parse_explicit_template_arguments(...)`.

### 4. Complete non-type template argument value model

Replace the concrete `int64_t`-centered NTTP identity with a structural
constant-value identity.

The model should represent:

- integral and enumeration values with exact type;
- `bool`, character types, signedness, and width;
- `nullptr`;
- pointer values, including object/function pointers and linkage identity;
- pointer-to-member values;
- references;
- floating-point values using exact representation;
- structural class-type template parameter objects;
- dependent constant expressions with preserved semantic expression identity.

Equivalence and hashing should implement C++20 template-argument equivalence,
not general expression equality.

### 5. Two-phase lookup and point-of-instantiation state

Template declarations should store a semantic definition context:

- ordinary unqualified lookup results from the definition point where required;
- namespace and class scope;
- using directives/declarations visible at definition;
- template parameter environment;
- current-instantiation identity;
- dependent lookup records to be completed at instantiation.

At instantiation, only dependent lookups should be completed with the
point-of-instantiation context. Non-dependent names should not be rebound to
later declarations.

### 6. Separate deduction from instantiation

Function-template calls, member-template calls, CTAD, and deduction guides
should share a semantic deduction engine.

The engine should produce either:

- a complete template argument list;
- a substitution failure;
- a hard diagnostic for non-SFINAE contexts.

It should not materialize a function body or class body while ranking
candidates. Instantiation should happen only after candidate selection and
constraint checking.

Required subareas:

- all C++20 non-deduced contexts;
- forwarding-reference rules;
- array/function parameter adjustments;
- cv/ref qualification transformations;
- template-template parameter deduction;
- pack deduction and pack consistency;
- overload-set deduction;
- conversion function template deduction;
- partial ordering;
- constraints and subsumption.

### 7. Constraint normalization and subsumption

Current concept checks should evolve into a C++20 constraints subsystem:

- normalize requires-clauses and constrained parameters into atomic
  constraints;
- substitute template arguments into constraints under SFINAE rules;
- compare atomic constraints for subsumption;
- use constraints in overload ordering and partial ordering;
- preserve diagnostics for failed associated constraints.

### 8. Instantiation context and substitution ownership

Substitution should operate on a single `TemplateInstantiationContext` rather
than many parallel vectors and maps.

The context should include:

- selected template declaration;
- parameter-to-argument bindings;
- pack bindings and pack expansion state;
- enclosing template contexts;
- current instantiation;
- point of instantiation;
- failure policy;
- lookup context;
- constexpr/sema context.

`TemplateEnvironment` can be the seed, but it should become declaration- and
scope-aware instead of only name-to-argument aware.

### 9. Class template and member template materialization

Class-template materialization should be split into phases:

1. create the instantiated type identity and current-instantiation context;
2. instantiate base-specifiers and member declarations as declarations;
3. register declarations and injected-class-name;
4. defer function bodies and static data member definitions until required;
5. sema-normalize ODR-used entities before codegen;
6. constant-evaluate only when C++ requires immediate evaluation.

Member function templates should use the same deduction and instantiation
context as free function templates, with an added object parameter and class
current-instantiation context.

### 10. Static members and constexpr evaluation

Static data member support should move away from early parser-driven repair.

The target behavior:

- declarations are instantiated with correct type and linkage identity;
- initializers are stored as semantic AST under the definition context;
- constant evaluation receives the instantiated semantic context;
- function calls inside initializers use the unified call-resolution service;
- ODR-use and immediate-constant contexts trigger materialization at the right
  time;
- normalized bytes are a post-sema/codegen artifact, not the source of truth.

## Migration strategy

### Phase 1: Document and instrument current behavior

- Add targeted tests for current known behavior before refactoring.
- Add debug assertions that identify when parser heuristics classify an argument
  without target parameter context.
- Track all direct `TemplateRegistry` lookups that act as semantic lookup.
- Track all substitution paths that use raw parameter-name vectors or positional
  maps instead of `TemplateEnvironment`.

### Phase 2: Introduce semantic template-id nodes

- Preserve template-id syntax without committing to type/value classification.
- Store argument syntax and source locations.
- Let semantic resolution classify arguments after template lookup.
- Keep existing `TemplateTypeArg` lowering as a compatibility output.

### Phase 3: Centralize template declaration lookup

- Route class/function/alias/variable template lookup through one semantic
  lookup result.
- Keep `TemplateRegistry` as a cache/index behind lookup.
- Replace unqualified-plus-qualified duplicate registration with declaration
  identity and lookup context.

### Phase 4: Build the conformance-oriented argument matcher

- Match parameters to argument syntax using the target template declaration.
- Fill defaults using the correct prior-parameter context.
- Expand packs only after parameter matching decides pack ownership.
- Produce `TemplateTypeArg` only after semantic classification.

### Phase 5: Replace NTTP identity

- Introduce a structural constant value representation.
- Adapt instantiation keys and mangling to consume it.
- Keep `int64_t` as a temporary carrier only for legacy integral cases.
- Add tests for pointers, references, member pointers, `nullptr`, enums,
  floating-point NTTPs, and structural class NTTPs as support lands.

### Phase 6: Implement two-phase lookup records

- Store definition-context lookup results for non-dependent names.
- Store dependent lookup records for names to complete at instantiation.
- Update expression, type, alias, and static-member initializer substitution to
  consume those records.

### Phase 7: Deduction engine extraction

- Extract deduction from parser instantiation functions into a semantic service.
- Make it produce candidate viability and complete template argument lists
  without materializing definitions.
- Use the same engine for free functions, member functions, constructors, CTAD,
  deduction guides, and conversion functions.

### Phase 8: Constraint subsystem

- Normalize constraints.
- Substitute constraints during candidate viability.
- Implement subsumption for overload and partial ordering.
- Replace current concept checks with this subsystem.

### Phase 9: Materialization fixpoint

- Make sema own all first materialization for ODR-used template entities.
- Run a pre-codegen materialization/normalization fixpoint.
- Convert codegen fallback paths into invariant checks once sema coverage is
  complete.

## Suggested regression coverage

### Template argument classification

- Type parameter versus non-type parameter with the same identifier spelling.
- Qualified type versus qualified static data member.
- Alias template used as type argument and variable template used as value
  argument.
- Dependent `typename T::type` and dependent `T::template rebind<U>`.
- Current-instantiation members and dependent base members.

### NTTPs

- Exact signedness and width distinctions.
- Enum NTTPs.
- `nullptr` NTTPs.
- Pointer/reference/member-pointer NTTPs.
- Floating-point NTTPs.
- Structural class NTTPs.
- Dependent NTTP expressions involving `sizeof`, `alignof`, `noexcept`, and
  static data members.

### Lookup and ADL

- Non-dependent unqualified names unaffected by later declarations.
- Dependent calls that find hidden friends by ADL at instantiation.
- Qualified lookup through dependent base classes.
- Namespace aliases, inline namespaces, using-directives, and using-declarations
  in template definitions and instantiations.

### Deduction

- Non-deduced contexts.
- Forwarding references.
- Overload-set arguments.
- Template-template parameter matching.
- Packs before, between, and after fixed parameters.
- CTAD and user-provided deduction guides.
- Constraint subsumption in overload ordering.

### Static members

- Static constexpr members used as NTTPs inside and outside class templates.
- Static data member initializers that call hidden friends or member templates.
- ODR-use-triggered instantiation timing.
- Dependent initializer failures that should be SFINAE versus hard errors.

## Recommended next step

Start with parameter-context-driven template argument classification. It is the
smallest structural change that directly reduces the largest class of current
non-conformance: deciding whether an argument is a type, value, or template
without first knowing the target parameter.
