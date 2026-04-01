# Nested Template Initializer Architecture Plan

## Current status

**Phases A, B, and D are implemented.**  Phase C (normalized initializer records)
remains future work (blocked by constexpr scope issue documented below).
Phase E (removing registry fallbacks) is deferred until the new model is proven
across the full test suite.

### What is done

- `TypeInfo::InstantiationContext` stores template parameter names, concrete
  arguments, and an optional parent pointer for nested types.
- Outer class-template instantiation populates the context (primary and
  specialization paths in `Parser_Templates_Inst_ClassTemplate.cpp`).
- Nested types created during class-template instantiation receive the
  enclosing type's context via the parent chain.
- `ConstExprEvaluator::load_template_bindings_from_type` and
  `try_load_current_struct_template_bindings` prefer the type-owned context
  before falling back to registry-name lookups.
- `IrGenerator_Visitors_TypeInit.cpp::evaluate_static_initializer` prefers
  the type-owned context; the outer-template-binding recovery path also
  checks the enclosing type first.
- Registry-based fallbacks are retained as safety nets but are no longer the
  primary lookup path for types that carry an InstantiationContext.
- `needs_default_constructor` is now set for nested structs during class-
  template instantiation (Phase D fix).
- `substituteTemplateParameters` now remaps nested struct types from the
  template pattern to the instantiated version (e.g., `Outer::Inner` →
  `Outer$hash::Inner`) so that constructor calls in lazy member function
  bodies reference the correct global symbol.
- `generateTrivialDefaultConstructors` sets `current_struct_name_` so that
  default member initializers can resolve unqualified static member references
  (e.g., `payload.a` resolving to `StructName::payload.a`).
- `resolveGlobalOrStaticBinding` updates the store name to use the instantiated
  struct qualification when the identifier was parsed against the template
  pattern but codegen runs in the instantiated context.
- Lazy static member instantiation now recursively materializes same-struct
  static-member identifier dependencies (e.g., `earlier = later;`) before
  freezing the initializer, so forward references no longer collapse to zero
  during codegen.

### What remains

- Phase C: Add `NormalizedInitializer` for static-storage members so that
  constexpr aggregate byte-packing happens before codegen (eliminates the
  retained-AST re-evaluation for common cases).  Blocked by the static member
  function resolution issue documented below.
- Phase E: Remove the registry/name-based compatibility fallbacks once the
  new owner model is proven across the full test suite.

## Investigation findings (2026-03-31)

### Phase C blocker: static member function resolution during early normalization

An attempt to add `normalizeStaticMemberInitializers()` — eagerly packing
literal values during instantiation — was reverted because it exposed a
pre-existing bug in constexpr evaluation during template substitution.

**Reproducer** (existing passing test
`test_identifier_binding_template_static_member_prefers_static_helper_ret42`):

```cpp
constexpr int helper() { return 7; }

template <typename T>
struct Box {
    static constexpr int helper() { return static_cast<int>(sizeof(T)) + 38; }
    static constexpr int value = helper();  // Should call Box::helper, not global
};

int main() { return Box<int>::value; }  // Expected: 42
```

During the existing substitution path at
`Parser_Templates_Inst_ClassTemplate.cpp:~4456`, the `ConstExpr::Evaluator`
resolves the unqualified `helper()` call to the **global** function (returning
7) instead of the class-scoped static member function (returning 42).  Without
early normalization, codegen re-evaluates with a fuller scope and gets 42.
Locking in the wrong value during instantiation broke the test.

**Root cause**: The `ConstExpr::EvaluationContext` created at substitution time
does not include the class-scope member functions, so unqualified function calls
resolve against global scope only.

**Recommended fix**: Either populate the evaluation context with the struct's
static member functions before evaluating, or restrict early normalization to
initializers that do not contain `FunctionCallNode`.

### Phase D bug: `needs_default_constructor` missing for nested structs

**Status**: The reproducer below now passes.  `needs_default_constructor` is set
for instantiated nested structs without explicit constructors, and
`generateTrivialDefaultConstructors()` consumes that flag before the main AST
walk to emit the synthetic default-constructor bodies.  Variable initialization
then lowers `Outer<42>::Inner obj;` through the normal `ConstructorCallOp`
path, which is why these cases link and run correctly.

**Reproducer** (passing):

```cpp
template <int N>
struct Outer {
    struct Inner {
        int tag = N;  // default member initializer references outer NTTP
    };
};

int main() {
    Outer<42>::Inner obj;
    return obj.tag;  // Returns 42
}
```

**Affected scope**: Any nested struct inside a template that:
- has default member initializers (especially NTTP-dependent ones), AND
- does not have an explicit constructor

Non-template nested structs are unaffected (they take a different code path
through the parser that sets the flag correctly).  Non-nested template structs
are also unaffected (line 6804 handles them).

### Additional test cases needed

**Phase D tests** (to be committed alongside the fix):

1. `test_template_nested_default_member_init_nttp_ret42.cpp` — the basic
   reproducer above.
2. `test_template_nested_default_member_init_sizeof_ret42.cpp` — default
   initializer using `sizeof(T)` instead of NTTP.
3. `test_template_nested_default_member_init_method_ret42.cpp` — nested
   struct created via member function (exercises struct copy with default
   init as well).
4. `test_template_nested_default_member_init_aggregate_ret42.cpp` — brace-
   initialized nested struct (`Outer<42>::Inner obj{}`; should still work
   after the fix).

### Verified working scenarios

The following related scenarios already work correctly and should be preserved
as non-regressions:

- Non-template nested struct with default member init → **works**
- Non-nested template struct with NTTP default member init → **works**
- Nested template struct with `static constexpr` members depending on outer
  NTTP → **works** (covered by `test_template_nested_inst_context_ret42`)
- Aggregate-initialized nested template struct (`Inner obj{}`) → **works**
  (aggregate init bypasses the default constructor path)

## Problem statement

FlashCpp already instantiates templates during parsing, but it still allows some
template-dependent initializer work to leak into code generation. That becomes
especially fragile for nested instantiated structs, where static-storage and
default-member initializers may still need template context after the template
body has been copied and substituted.

The current nested static-struct bug is a concrete example:

- top-level `static constexpr` aggregate objects were recently shown to reach
  codegen through the static-member path with the wrong initializer shape until
  the parser path was aligned with normal variable initialization
- nested template static aggregate objects now preserve the aggregate shape, but
  their substituted leaf values can still collapse to zero during codegen-time
  constexpr evaluation because nested instantiated structs do not carry a strong,
  first-class template environment

This is a design issue, not just a one-off runtime bug.

## Executive summary

The long-term fix is **not** "move template instantiation earlier" in the broad
sense. Template instantiation already happens early enough. The real problem is
that the compiler does not yet enforce a hard **post-instantiation normalized
initializer boundary**.

Today:

- template instantiation substitutes many things early
- but static/default initializers can still remain as retained AST that codegen
  must re-interpret
- codegen then reconstructs template bindings indirectly from names, lazy
  registries, and AST-attached metadata
- nested instantiated structs are the weakest case in that scheme

Desired end state:

- instantiation produces a type-local, explicit template environment
- static-storage and default-member initializers are normalized before codegen
- codegen consumes already-normalized AST or pre-materialized constant bytes
- codegen no longer needs to rediscover template bindings for nested structs

## Why this is currently a codegen problem

Codegen currently owns several jobs that should be upstream or at least have
stronger upstream inputs:

1. It decides whether a static initializer is scalar, aggregate, relocatable, or
   requires constexpr evaluation.
2. It reconstructs constexpr evaluation context from:
   - `StructTypeInfo*`
   - lazy instantiation registries
   - `gTemplateRegistry` outer binding lookups by qualified name
   - AST node metadata when available
3. It attempts to re-evaluate retained AST late, after template instantiation,
   sema normalization, and other pipeline phases have already run.

That is survivable for simple outer instantiations, but nested cases are brittle
because:

- the instantiated nested `StructTypeInfo` does not itself own the full
  instantiation environment in a canonical way
- some metadata lives on copied AST nodes, some on global registries, and some is
  inferred from naming conventions
- codegen has to guess which source of truth is authoritative

This is why the same general mechanism can work for one case and silently lose
bindings in another.

## Root causes

### 1. No single owner for post-instantiation initializer state

For struct members and static members, there is no single canonical "normalized
initializer" representation after instantiation.

Instead, the compiler currently relies on combinations of:

- parser-copied AST
- substituted AST
- AST nodes with outer-template binding metadata
- `StructStaticMember.initializer`
- `StructMember.default_initializer`
- late constexpr evaluation in codegen

That makes it easy for two paths to diverge.

### 2. Template environment is attached to the wrong places

Outer-template bindings are attached to many AST nodes and registry entries, but
not to the instantiated type itself in a way that makes late consumers safe.

For nested instantiated structs, the most natural source of truth should be:

- the instantiated `TypeInfo`
- or the instantiated `StructTypeInfo`

Instead, the environment often has to be recovered from:

- copied declaration nodes
- member function nodes
- global registry entries keyed by mangled/qualified names

That recovery step is exactly where nested cases become fragile.

### 3. Static/default initializer evaluation is duplicated across phases

There are multiple initializer-handling paths for:

- global variables
- static data members
- aggregate default member initialization
- nested structs and arrays

Those paths are similar, but not unified under one normalized representation.

### 4. Codegen is acting as a second template-aware evaluator

Once codegen needs to interpret template-dependent retained AST, it effectively
becomes another template-aware phase. That increases drift risk:

- parser/instantiation owns one substitution model
- sema owns one normalization model
- codegen owns another recovery/evaluation model

Nested cases expose those differences first.

## Architectural goals

1. **Single source of truth**
   Every instantiated struct should carry enough information for later phases to
   understand its template environment without registry-name reconstruction.

2. **Explicit post-instantiation boundary**
   After class-template instantiation completes, initializer state must be in one
   of a small number of well-defined forms.

3. **Codegen should consume, not rediscover**
   Codegen should emit data from normalized initializer state rather than
   re-deriving template bindings from global registries.

4. **Nested instantiations should be no weaker than outer instantiations**
   A nested instantiated struct must have the same quality of type-local template
   metadata as the outer instantiated struct.

5. **Incremental migration**
   The solution should land in narrow, bisectable slices and improve behavior
   before full cleanup is complete.

## Desired invariants

### Invariant A: instantiated types own their instantiation context

Every instantiated class/nested-class `TypeInfo` should optionally carry a
canonical instantiation context:

- base template/pattern identity
- ordered outer parameter names
- ordered concrete outer arguments
- optional parent instantiation context for nesting

This should be directly accessible without qualified-name registry lookups.

### Invariant B: static-storage initializers are normalized before codegen

For globals and static members, after instantiation and normalization the
initializer should be in exactly one of these forms:

- `ConstantBytes`
- `Relocation`
- `NormalizedAst` that is explicitly marked as requiring later emission
- `Uninitialized`

The common `constexpr struct aggregate` case should end up as `ConstantBytes`,
not retained AST that codegen has to reinterpret.

### Invariant C: default member initializers see the type-local environment

Default member initializers that survive to later evaluation must evaluate
against the instantiated type's own environment, not against a reconstructed
name-based environment.

### Invariant D: codegen never asks "what template bindings apply here?"

By the time codegen sees a static object or default member initializer, the
binding environment should already be provided by the owning type or the
initializer record itself.

## Proposed design

## 1. Add a canonical `InstantiationContext` owned by instantiated types

Introduce a small shared structure, conceptually:

```cpp
struct InstantiationContext {
	InlineVector<StringHandle, 4> param_names;
	InlineVector<TypeInfo::TemplateArgInfo, 4> param_args;
	StringHandle pattern_name;
	const InstantiationContext* parent = nullptr;
};
```

Attach this to instantiated `TypeInfo` and/or `StructTypeInfo`.

Recommended ownership:

- `TypeInfo` owns the storage
- `StructTypeInfo` exposes a cheap accessor to the same context

Why:

- `TypeInfo` already represents the concrete instantiated type
- nested and alias-followed consumers already tend to reach `TypeInfo`
- codegen/sema/constexpr can all share one accessor

### Requirements

- set for outer class-template instantiations
- set for nested instantiated structs copied during class-template instantiation
- preserve ordering exactly as instantiated
- support parent-child nesting without recomputing the full combined list every
  time

## 2. Introduce a reusable initializer normalization layer

Create a dedicated normalization step for initialized storage and retained member
initializers.

Possible API shape:

```cpp
struct NormalizedInitializer {
	enum class Kind {
		Uninitialized,
		ConstantBytes,
		Relocation,
		NormalizedAst,
		DynamicInitializationRequired
	};

	Kind kind;
	std::vector<char> constant_bytes;
	StringHandle reloc_target;
	std::optional<ASTNode> ast;
};
```

This does **not** need to replace all existing fields on day one. It can be
introduced alongside current fields and migrated incrementally.

### Ownership targets

- global variables
- `StructStaticMember`
- optionally `StructMember.default_initializer` if/when default-member
  normalization is split from raw parser AST

## 3. Split "substitution" from "materialization"

The compiler currently substitutes template parameters, but often postpones
initializer materialization until codegen.

Those should become two explicit steps:

1. **Substitution**
   - rewrite AST into instantiated form
   - attach canonical instantiation context to the owning type

2. **Materialization**
   - classify initializer as bytes / relocation / retained AST
   - evaluate compile-time aggregate/scalar initializers while the instantiation
     context is still explicit

This keeps the retained AST path available for harder cases without forcing
codegen to be the default materializer.

## 4. Make constexpr evaluation context type-driven, not registry-driven

Add a helper that builds constexpr evaluation context from an instantiated type:

```cpp
ConstExpr::EvaluationContext makeEvaluationContextForType(
	const StructTypeInfo& owner,
	SymbolTable& symbols,
	Parser* parser);
```

This helper should:

- use the owner's canonical `InstantiationContext`
- populate template parameter names/args directly
- only use registries for genuinely lazy or deferred behavior, not for ordinary
  environment recovery

This becomes the shared entry point for:

- static member constant evaluation
- default member initializer evaluation
- any remaining type-owned constexpr materialization

## 5. Move static aggregate byte materialization out of ad hoc codegen branches

The aggregate byte-packing logic should become a shared service used by:

- global variable initialization
- static member initialization
- eventual normalized initializer materialization

That service should operate on:

- normalized initializer AST
- concrete `StructTypeInfo`
- a provided scalar-evaluation callback bound to a canonical evaluation context

The recent local helper extraction is a good stepping stone, but the long-term
goal is to make this part of the initializer normalization layer, not just a
codegen utility.

## 6. Treat codegen fallback lookups as temporary compatibility bridges

The following patterns should be considered transitional:

- `gTemplateRegistry.getOuterTemplateBinding(...)` during codegen-time evaluation
- reconstructing template arguments from `TypeInfo::baseTemplateName()`
- special handling that depends on qualified-name string shapes

Keep them only while the new type-owned context is being rolled out.

## Recommended implementation plan

## Phase A: make instantiated type context explicit

### Goal

Ensure every instantiated outer/nested class carries a canonical template
environment.

### Work

1. Add `InstantiationContext` storage/accessors on `TypeInfo` and/or
   `StructTypeInfo`.
2. Populate it in outer class-template instantiation.
3. Populate it in nested class instantiation using the already-available outer
   binding data.
4. Add assertions/logging for nested instantiated structs missing context.

### Validation

- focused nested instantiation tests
- debug assertions/logging during existing template suites

### Expected payoff

This alone should make it much easier to diagnose any remaining nested failures,
because consumers can inspect the owning type instead of guessing from names.

## Phase B: introduce type-driven constexpr evaluation helpers

### Goal

Stop building evaluation context differently in parser, sema, and codegen for
type-owned initializers.

### Work

1. Add `makeEvaluationContextForType(...)`.
2. Refactor codegen static-member evaluation to use it.
3. Refactor default member initializer evaluation paths to use it.
4. Keep old registry fallback only as a safety net while coverage is expanded.

### Validation

- nested static scalar and aggregate regressions
- retained nested default member initializer regressions
- mixed-order template-parameter binding tests

### Expected payoff

This reduces the number of places that can accidentally diverge on template
binding recovery.

## Phase C: add normalized initializer records for static storage

### Goal

Move common constant-initialization work out of late codegen interpretation.

### Work

1. Add `NormalizedInitializer` for global/static storage.
2. Teach instantiation or post-instantiation normalization to classify:
   - constant bytes
   - relocations
   - retained AST
3. Store constant aggregate/scalar bytes directly on static members when possible.
4. Update codegen to prefer normalized initializer records over raw AST.

### Validation

- top-level static scalar/aggregate tests
- nested template static scalar/aggregate tests
- mutable inline static struct tests
- inherited static member emission paths

### Expected payoff

Most of the problematic current cases should stop depending on late AST
re-evaluation entirely.

## Phase D: normalize default member initializers

### Goal

Make retained member initializers for instantiated structs depend on type-local
context, not parser/global registries.

### Work

1. Add a normalization path for `StructMember.default_initializer`.
2. Preserve either:
   - normalized AST with explicit owner context
   - or materialized constant value where appropriate
3. Refactor object construction paths to consume normalized member initializers.

### Validation

- nested class default member initializer tests
- template partial specialization member initializer tests
- nested static-member access from default initializers

### Expected payoff

This closes the exact category of bug where `int value = payload.a + payload.b;`
works only when codegen can rediscover the outer environment.

## Phase E: remove registry/name-based compatibility paths

### Goal

Delete fragile late recovery once the new owner model is proven.

### Work

1. Remove codegen use of outer-template binding recovery by qualified-name string.
2. Remove duplicated template-arg reconstruction from `baseTemplateName()` where
   the instantiated type already has direct context.
3. Tighten assertions so missing instantiated-type context becomes a bug, not a
   silent zero-initialization path.

### Validation

- full Windows suite
- Linux suite if practical
- targeted logging disabled/clean

## Design choices and recommendations

## Recommendation 1: do not move all constexpr materialization into sema

That would be a larger architectural jump and would entangle this fix with other
 compiler responsibilities.

Better:

- keep parser/instantiation responsible for substitution and context attachment
- add a small post-instantiation normalization/materialization layer
- let sema and codegen consume the normalized result

## Recommendation 2: prefer type-owned metadata over AST-owned metadata

AST-attached outer bindings are still useful, especially for copied function and
lambda bodies. But for type-owned initializer evaluation, the owning type should
be authoritative.

Reason:

- types survive longer and are easier to find
- nested instantiations are inherently type-centric
- this avoids string-key registry lookups in late phases

## Recommendation 3: keep raw AST only as a compatibility layer

Do not try to eliminate retained initializer AST immediately. Instead:

- add normalized records beside current fields
- migrate the stable constant cases first
- leave hard/dynamic cases on raw AST until the new path is proven

This keeps the migration bisectable.

## Recommendation 4: unify initializer classification logic

There should eventually be one classifier for:

- scalar constant initializers
- aggregate constant initializers
- relocation-bearing initializers
- dynamic/unsupported cases

The same classification should be used whether the initializer belongs to:

- a global variable
- a static data member
- an instantiated nested static data member

## Test plan for the migration

Minimum permanent coverage should include:

1. top-level static constexpr aggregate object:
   - `test_static_constexpr_struct_aggregate_runtime_ret42.cpp`

2. nested template static aggregate object:
   - `test_template_nested_static_struct_member_ret45.cpp`

3. nested template default member initializer reading nested static object:
   - current regression shape using `payload.a + payload.b`

4. mutable inline static struct object:
   - read/write behavior through static storage

5. partial specialization + nested initializer context:
   - mixed-order template argument binding

6. retained nested member initializer coverage:
   - ensure sema/codegen agree on normalized values

## Risks

### Risk 1: metadata duplication

If outer-template bindings remain on AST nodes *and* are added to types, the two
can diverge.

Mitigation:

- define the type-owned context as authoritative for type-owned initializer
  evaluation
- keep AST-owned bindings for body-local normalization only

### Risk 2: constant-vs-dynamic classification drift

Some initializers may not yet be fully constexpr-materializable.

Mitigation:

- make `NormalizedInitializer::Kind` explicit
- do not force all cases into `ConstantBytes`
- preserve a retained-AST fallback while migrating

### Risk 3: alias and nested-name confusion

Some current code follows aliases or reconstructs names opportunistically.

Mitigation:

- add helper accessors that resolve the owning concrete instantiated type once
- use those helpers consistently in evaluation/materialization

## Immediate next slice recommendation

The best next implementation slice is:

1. add type-owned instantiation context to instantiated `TypeInfo` /
   `StructTypeInfo`
2. populate it for nested instantiated structs
3. switch static/default initializer constexpr evaluation helpers to consume that
   context directly
4. keep existing registry fallback only behind a compatibility path

That is narrow enough to land safely and directly targets the remaining nested
`ret45` failure.

## Success criteria

This plan is complete when all of the following are true:

- nested instantiated structs carry direct, inspectable outer-template context
- static-storage constexpr aggregate initialization does not depend on codegen
  name-based template recovery
- default member initializers in nested instantiated structs evaluate correctly
  using type-local context
- codegen no longer needs special-case outer-template registry recovery for the
  common initialized-storage paths
- the focused nested regression passes and remains stable under full-suite
  validation

## Final recommendation

Do **not** treat this as "templates are resolved too late."

Treat it as:

> instantiated types do not yet own a complete, normalized initializer view, so
> codegen is forced to reconstruct template context too late and too indirectly.

The long-term fix is to make instantiated type context explicit and to normalize
initializer state before codegen, not to make codegen smarter.
