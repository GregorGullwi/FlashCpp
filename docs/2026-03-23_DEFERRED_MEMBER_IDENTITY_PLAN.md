# Deferred Member Identity Plan

## Current status

This issue became concrete while fixing the non-`constexpr` conversion-operator
bugs in `docs/KNOWN_ISSUES.md`.

The current compiler can now canonicalize some instantiated conversion-operator
names, but it still has two fragile deferred-member handoff points:

- `DeferredTemplateMemberBody` still stores only a raw `function_name` handle and
  then matches bodies back to instantiated members by name plus `const`
- `LazyMemberFunctionInfo` stores the original function AST plus one member name,
  but that name can drift from the declaration token actually emitted for the
  instantiated body

That is enough to reproduce:

- unresolved external references to canonicalized conversion operators such as
  `operator int`
- emitted lazy bodies still named `operator user_defined` or `operator T`
- runtime mismatches when the signature-only stub is found but the body is
  materialized under a different name

This is no longer just a local bug. It is a data-model problem.

## Problem statement

Deferred member work currently relies on **string matching** where it should rely
on **member identity**.

Today the template pipeline effectively has three different names for the same
member function:

1. the original parsed declaration token (`operator value_type`, `operator T`)
2. the canonical instantiated lookup name (`operator int`, `operator float`)
3. the name later re-derived while deferred bodies or lazy bodies are rebuilt

Those names are not guaranteed to stay in sync across:

- eager instantiation
- deferred body replay
- lazy member instantiation
- `StructTypeInfo` registration
- outer-template binding registration
- mangling/codegen emission

The current structures are too weak for this:

- `DeferredTemplateMemberBody` (`src/AstNodeTypes_Core.h`) stores only
  `function_name`, `is_const_method`, and body-position data
- `LazyMemberFunctionInfo` (`src/TemplateRegistry_Lazy.h`) stores the original AST
  node plus one `member_function_name`, but still treats the string as the lookup
  identity

As a result, the compiler keeps rebuilding identity from names instead of carrying
the identity through the pipeline.

## Executive summary

The right fix is to introduce a **richer deferred member identity** that travels
with every deferred/lazy member record.

That identity should:

- point at the original source member declaration
- record the member kind and qualifiers
- record the canonical instantiated lookup name once it is known
- let instantiation connect the original source member directly to its
  instantiated stub/body without rescanning by string

The key design rule is:

> Deferred member matching should be source-member-driven, not name-driven.

## Proposed data structure

The recommended shape is:

```cpp
struct DeferredMemberIdentity {
	enum class Kind : uint8_t {
		Function,
		Constructor,
		Destructor,
	};

	Kind kind;

	ASTNode original_member_node;              // authoritative source declaration
	StringHandle template_owner_name;          // e.g. integral_constant
	StringHandle instantiated_owner_name;      // e.g. integral_constant$hash

	StringHandle original_lookup_name;         // parsed spelling: operator value_type / operator T
	StringHandle instantiated_lookup_name;     // canonical spelling: operator int / operator float

	OverloadableOperator operator_kind;        // valid when this is an operator overload
	bool is_operator;
	bool is_const_method;
	CVQualifier cv_qualifier;
	ReferenceQualifier ref_qualifier;
	uint16_t parameter_count;
};
```

Then the current deferred/lazy records become:

```cpp
struct DeferredTemplateMemberBody {
	DeferredMemberIdentity identity;
	SaveHandle body_start;
	SaveHandle initializer_list_start;
	size_t struct_type_index;
	bool has_initializer_list;
	InlineVector<StringHandle, 4> template_param_names;
};

struct LazyMemberFunctionInfo {
	DeferredMemberIdentity identity;
	InlineVector<ASTNode, 4> template_params;
	InlineVector<TemplateTypeArg, 4> template_args;
	AccessSpecifier access;
	bool is_virtual;
	bool is_pure_virtual;
	bool is_override;
	bool is_final;
};
```

## Why this shape

### 1. `original_member_node` becomes the source of truth

The compiler already has stable AST ownership for the original template member.
That means the deferred body record does not need to “find” the source member by
name later; it can carry the member directly.

This removes the current failure mode where:

- the template definition stored `operator value_type`
- the instantiated stub was registered as `operator int`
- the deferred-body replay only knew `operator value_type` and failed to match

### 2. `instantiated_lookup_name` becomes explicit state

Right now the canonical instantiated name is recomputed in several places. That is
exactly where the current drift comes from.

Instead:

- compute the instantiated lookup name once during member-stub creation
- store it in the identity
- use that same handle for:
  - `StructTypeInfo` registration
  - lazy-registry keys
  - deferred-body target lookup
  - outer-template binding registration
  - emitted declaration tokens for lazy bodies

### 3. matching becomes structural rather than textual

`function_name + const` is not a durable identity.

It is especially weak for:

- conversion operators whose names change after substitution
- nested class members
- future cases with ref-qualified overloads
- any future work that wants to normalize declaration spelling before replay

With a richer identity, the compiler can match:

- by original source member
- then by explicitly stored canonical instantiated name
- and only use name-based scanning as a temporary migration fallback

## Proposed lifecycle

### Phase 1: create identity at template-definition time

When building `DeferredTemplateMemberBody` and lazy-member records:

- populate `identity.original_member_node`
- populate `identity.kind`
- populate `identity.original_lookup_name`
- populate operator/qualifier metadata
- leave `identity.instantiated_owner_name` and
  `identity.instantiated_lookup_name` empty until instantiation

This is the point where the compiler still knows the exact original source member
without any reconstruction.

### Phase 2: bind identity during class-template instantiation

When creating each instantiated member stub:

- compute the canonical instantiated return type
- compute the canonical instantiated member name
- fill `identity.instantiated_owner_name`
- fill `identity.instantiated_lookup_name`
- register the stub under that exact identity

At the same time, build a per-instantiation map such as:

```cpp
std::unordered_map<ASTNode, ASTNode, AstNodeHash> source_member_to_instantiated_stub;
```

or an equivalent handle-based map if `ASTNode` hashing is inconvenient.

This map is the bridge from source member to instantiated placeholder/body.

### Phase 3: replay deferred bodies through the identity map

When parsing deferred bodies:

- stop scanning `instantiated_struct_ref.member_functions()` by
  `deferred.function_name`
- instead resolve the target stub from
  `identity.original_member_node -> instantiated stub`
- use `identity.instantiated_lookup_name` only for diagnostics/assertions

This removes the current name-remap workaround entirely.

### Phase 4: make lazy materialization consume the same identity

`instantiateLazyMemberFunction()` should:

- read the original member from `identity.original_member_node`
- resolve aliases from the owning template if needed
- emit the rebuilt declaration token using
  `identity.instantiated_lookup_name`
- register outer-template bindings using the same handle
- update `StructTypeInfo` using the same handle

This makes lazy and eager materialization share the same notion of member
identity.

### Phase 5: remove string-repair code

Once the identity is threaded through:

- remove name-based deferred-body rescue logic
- remove ad-hoc conversion-operator renaming patches that exist only to bridge
  between stale names
- keep one canonical name computation helper, but only for the moment identity is
  first bound during instantiation

## Required invariants

After this change, these should always hold:

1. `identity.original_member_node` always refers to the original template member.
2. `identity.instantiated_lookup_name` always matches the instantiated stub’s
   declaration token.
3. `identity.instantiated_lookup_name` is the exact key used in
   `StructTypeInfo`, lazy registries, and outer-template binding registration.
4. Deferred body replay never needs to rediscover the target member by scanning
   names.
5. Lazy member materialization never emits a body under a different name from the
   stub that requested it.

## Migration plan

### Slice 1: add the new identity struct

- add `DeferredMemberIdentity`
- thread it into `DeferredTemplateMemberBody`
- thread it into `LazyMemberFunctionInfo`
- populate the original/source-side fields only

This slice should be mostly mechanical.

### Slice 2: bind instantiated names once

- introduce one helper that computes the canonical instantiated lookup name from:
  - original declaration
  - substituted return type
  - instantiated owner
- store the result in the identity when the stub is created
- switch `StructTypeInfo` and lazy-registry registration to the identity handle

This should remove most conversion-operator drift.

### Slice 3: replace deferred-body name scans

- build a source-member-to-instantiated-stub map during instantiation
- use it when replaying deferred member bodies
- keep the old name scan only as a temporary debug fallback with a loud log

### Slice 4: switch lazy materialization fully to identity

- stop rebuilding declaration identity from the original declaration token
- rebuild the emitted declaration directly from the identity
- remove the temporary alias-resolution duplication if it becomes redundant

### Slice 5: cleanup and assertions

- add assertions/logging that stub name, identity name, and emitted body name all
  agree
- remove transitional remap code
- document the new invariant in nearby comments

## Testing plan

The minimum regression set for this work should include:

- `operator value_type()` on a class template instantiated to `int`
- `operator value_type()` instantiated to multiple concrete primitive types
- direct `operator T()` on a class template instantiated to `int` and `float`
- inherited conversion operators through a templated base
- a non-conversion member function with deferred body replay to verify that the
  new identity does not regress ordinary members
- nested template member functions if they also flow through the same deferred
  machinery

Validation should check:

- sema finds the conversion operator
- codegen emits the body under the same canonical name the caller references
- no unresolved external remains for the canonical operator name

## Non-goals

This plan does **not** attempt to:

- redesign the full template registry
- move all template instantiation earlier
- solve the broader nested-initializer architecture problem documented in
  `docs/2026-03-21_NESTED_TEMPLATE_INITIALIZER_ARCHITECTURE_PLAN.md`

It is a smaller, member-identity-specific architectural repair.

## Immediate next slice recommendation

If this work is resumed, the best first implementation slice is:

1. add `DeferredMemberIdentity`
2. store `original_member_node` in both deferred-body and lazy-member records
3. store `instantiated_lookup_name` when the signature-only stub is created
4. replace deferred-body replay’s name scan with source-member-based lookup

That slice should address the current conversion-operator failures directly while
keeping the change bounded.

### Slice 6: const/non-const conversion operator overload disambiguation

The current `LazyMemberInstantiationRegistry` uses `"class::func_name"` as the key
without encoding constness.  When a class template has both `operator T() const` and
`operator T()`, both map to the same key (`"LazyWrapper$hash::operator int"`), so only
the last registration is kept and both stubs are materialized from the same
`LazyMemberFunctionInfo`.

Required changes:
- Encode constness in the registry key:
  `"class::func_name"` → `"class::func_name"` (non-const) / `"class::func_name$const"` (const)
- Update `registerLazyMember`, `needsInstantiation`, `getLazyMemberInfo`, and
  `markInstantiated` in `TemplateRegistry_Lazy.h` to accept or derive a `bool is_const`
  flag from `DeferredMemberIdentity::is_const_method`.
- Update all call sites that currently pass only class name + function name.
- Update `emitConversionOperatorCall` in `IrGenerator_MemberAccess.cpp` to pass
  `conv_op.is_const()` to all registry lookups.
- Update stub-creation in `Parser_Templates_Inst_ClassTemplate.cpp` to register lazy stubs
  under the const-aware key.
