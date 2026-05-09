# Template Binding Architecture Plan

**Date:** 2026-05-09  
**Status:** In Progress (Phases 1-3 completed)  
**Scope:** Template argument identity, parameter binding environments, instantiation consumers, and C++20 failure-mode boundaries.

## Next Task

Implement Phase 4: replace outer binding arrays with binding snapshots while dual-writing legacy fields during migration.

Existing helpers now cover part of this work:

- `convertToTemplateArgInfo(...)` in `src/Parser_Core.cpp`
- `toTemplateTypeArg(...)` in `src/TemplateRegistry_Pattern.h`
- `materializeTemplateArg(...)` and `materializeTemplateArgs(...)` in `src/TemplateRegistry_Types.h`

Add a small environment-facing header/source pair:

- `src/TemplateEnvironment.h`
- `src/TemplateEnvironment.cpp`

Start by moving or wrapping the existing helpers behind that stable interface. Keep the old helper names as forwarding wrappers initially to avoid one large churn commit.

```cpp
TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg);
TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg);
InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args);
InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args);
```

Then replace the remaining duplicated conversion loops in these places:

- `src/AstNodeTypes_Expr.h`: `LambdaExpressionNode::set_outer_template_bindings`
- `src/AstNodeTypes_Template.h`: `TemplateClassDeclarationNode::set_outer_template_bindings`
- `src/Parser_Templates_Inst_ClassTemplate.cpp`: calls that build `TypeInfo` template instantiation metadata
- `src/Parser_Templates_Lazy.cpp`: lazy member setup that converts stored outer args

Acceptance criteria:

- no behavior change;
- no new default parameters;
- conversion round-trip preserves cv/ref/pointer/function-signature/dependent-expression metadata already represented by `TemplateArgInfo`;
- old helper names still compile and forward to the centralized implementation;
- add one internal/unit-style test if the repo has an existing low-friction place for helper tests, otherwise rely on focused template tests;
- run `pwsh tests/run_all_tests.ps1`.

## Phase Checklist

### Phase 1: Consolidate Argument Conversion Helpers ✅ Completed

Goal: make the existing conversion/materialization helpers one authoritative interface before changing data ownership.

Steps:

1. Add `src/TemplateEnvironment.h` with conversion declarations.
2. Add `src/TemplateEnvironment.cpp` with conversion implementations or forwarding definitions.
3. Move or wrap:
   - `convertToTemplateArgInfo(...)` from `src/Parser_Core.cpp`;
   - `toTemplateTypeArg(...)` from `src/TemplateRegistry_Pattern.h`;
   - `materializeTemplateArg(...)` and `materializeTemplateArgs(...)` from `src/TemplateRegistry_Types.h`.
4. Keep old helper names as forwarding wrappers for one or two cleanup commits.
5. Include the new helper where current code hand-copies between `TemplateTypeArg` and `TypeInfo::TemplateArgInfo`.
6. Replace only mechanical conversion loops.
7. Run targeted template tests, then full suite.

Do not:

- introduce `TemplateEnvironment` behavior yet;
- change `TemplateInstantiationKey`;
- change failure behavior;
- change class/function instantiation flow.

Completed notes:

- Added centralized conversion helpers in `src/TemplateEnvironment.h/.cpp`.
- Kept `convertToTemplateArgInfo(...)` and `toTemplateTypeArg(...)` call paths compiling through centralized logic.
- Replaced duplicated conversion loops in `src/AstNodeTypes_Expr.h`, `src/AstNodeTypes_Template.h`, and `src/Parser_Templates_Inst_ClassTemplate.cpp`.
- Verified with `pwsh tests/run_all_tests.ps1`.

Complexity: S  
Risk: Low

### Phase 2: Add `TemplateEnvironment` As An Adapter ✅ Completed

Goal: create one lookup object for "parameter name -> bound argument(s)" while leaving old storage in place.

The last commit added a local context-binding resolver in `ExpressionSubstitutor::materializeStoredTemplateArgs(...)`. Treat that as the first extraction target: it is already doing parent `TypeInfo::InstantiationContext` lookup and transitive dependent-name rebinding, which is exactly the behavior `TemplateEnvironment` should own.

Add to `src/TemplateEnvironment.h`:

```cpp
struct TemplateBinding {
	StringHandle name;
	TemplateParameterKind kind = TemplateParameterKind::Type;
	bool is_pack = false;
	InlineVector<TemplateTypeArg, 1> args;
};

struct TemplateEnvironment {
	InlineVector<TemplateBinding, 4> bindings;
	const TemplateEnvironment* parent = nullptr;

	const TemplateTypeArg* findOne(StringHandle name) const;
	std::span<const TemplateTypeArg> findPack(StringHandle name) const;
};
```

Add builders:

```cpp
TemplateEnvironment buildTemplateEnvironment(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent);

TemplateEnvironment buildTemplateEnvironment(const OuterTemplateBinding& binding);
TemplateEnvironment buildTemplateEnvironment(const TypeInfo::InstantiationContext& context);
```

Add a shared lookup helper:

```cpp
std::optional<TemplateTypeArg> resolveContextBinding(
	StringHandle target_name,
	const TemplateEnvironment& environment);
```

It should subsume the local `resolveContextBinding` lambda currently in `src/ExpressionSubstitutor.cpp`.

First call sites:

- `ExpressionSubstitutor::materializeStoredTemplateArgs(...)`: replace the local `resolveContextBinding` lambda with the shared helper.
- `collectOuterTemplateBinding(...)`: build or wrap an environment before writing legacy fields.
- `registerTypeParamsInScope(...)`: add overload accepting `const TemplateEnvironment&`.
- `populateTemplateParamSubstitutions(...)`: add overload accepting `const TemplateEnvironment&`.
- `buildSubstitutionParamMap(...)`: add overload or replacement that reads `TemplateEnvironment`.

Acceptance criteria:

- old fields remain populated;
- new adapter can represent parent class binding plus inner member-template binding;
- context binding resolution still handles transitive dependent aliases such as `_Tp -> _Head -> NonEmpty`;
- recursive/self-referential context bindings are cycle-safe;
- no current call site is forced to migrate until tests are green.

Example to protect:

```cpp
template <typename T>
struct Box {
	template <typename U>
	U cast(T value);
};

Box<int> b;
auto x = b.cast<long>(1);
```

The environment visible inside `cast<long>` must resolve `T -> int` and `U -> long`.

Also protect the alias-chain/default-NTTP scenario covered by the latest tests:

```cpp
template<class T>
using EmptyAndNotFinal = PickTypeT<__is_final(T), FalseTag, AliasSelectedBranch<T>>;

template<int Index, class Head, bool = EmptyAndNotFinal<Head>::value>
struct HeadSlot;

TupleLike<0, NonEmpty>* ptr = nullptr;
```

The environment must be able to resolve stored helper-template parameter names through the active consumer binding, not only by direct name match.

Completed notes:

- Added `TemplateBinding` and `TemplateEnvironment` with `findOne` and `findPack`.
- Added builders for parameter/argument spans, `OuterTemplateBinding`, and `TypeInfo::InstantiationContext`.
- Added shared `resolveContextBinding(...)` with cycle-safe transitive rebinding.
- Replaced local `resolveContextBinding` lambda in `ExpressionSubstitutor::materializeStoredTemplateArgs(...)`.
- Added environment-based overloads for `registerTypeParamsInScope`, `populateTemplateParamSubstitutions`, and `buildSubstitutionParamMap`.
- Updated `collectOuterTemplateBinding(...)` to build from `TemplateEnvironment` before writing legacy fields.
- Verified with `pwsh tests/run_all_tests.ps1`.

Complexity: M  
Risk: Medium

### Phase 3: Move Substitution Consumers To The Environment ✅ Completed

Goal: stop reconstructing maps/order/pack metadata in every consumer.

Migrate in this order:

1. `ExpressionSubstitutor`
2. `ConstExpr::EvaluationContext`
3. default template argument substitution
4. dependent NTTP expression evaluation
5. lazy static/member function instantiation

Files to touch:

- `src/ExpressionSubstitutor.h`
- `src/ExpressionSubstitutor.cpp`
- `src/ConstExprEvaluator.h`
- `src/ConstExprEvaluator_Core.cpp`
- `src/Parser_Templates_Inst_Deduction.cpp`
- `src/Parser_Templates_Inst_MemberFunc.cpp`
- `src/Parser_Templates_Lazy.cpp`

Add a failure policy enum near the environment or parser template helpers:

```cpp
enum class TemplateSubstitutionFailurePolicy {
	SfinaeProbe,
	HardUse,
	ShapeOnly
};
```

Consumer rules:

- `SfinaeProbe`: immediate-context substitution failure returns no candidate.
- `HardUse`: invalid concrete substitution throws `CompileError` or `InternalError`.
- `ShapeOnly`: class shape may preserve dependent placeholders but must mark artifacts as shape-only.

Acceptance criteria:

- body substitution failure for a selected template is not treated as SFINAE;
- immediate-context return-type/constraint substitution still supports SFINAE;
- constexpr consumers can report "dependent", "invalid", or "evaluated" distinctly.

Completed notes:

- Added `TemplateSubstitutionFailurePolicy` and parser-side policy wiring.
- Migrated substitution consumers (ExpressionSubstitutor, constexpr evaluation paths, default-arg/dependent-NTTP substitution paths, and lazy instantiation paths) to consume `TemplateEnvironment`-based bindings while preserving legacy behavior.
- Kept immediate-context probing behavior SFINAE-friendly while preserving hard-failure behavior for selected-template body substitution.
- Extended relevant contexts to carry environment bindings for dependent/evaluated/invalid distinction in constexpr-related flows.
- Verified with `pwsh tests/run_all_tests.ps1`.

Complexity: M-L  
Risk: Medium

### Phase 4: Replace Outer Binding Arrays With Snapshots

Goal: remove repeated `outer_template_param_names_` plus `outer_template_args_` storage.

Add snapshot structs:

```cpp
struct TemplateBindingSnapshot {
	StringHandle name;
	TemplateParameterKind kind = TemplateParameterKind::Type;
	bool is_pack = false;
	InlineVector<TypeInfo::TemplateArgInfo, 1> args;
};

struct TemplateEnvironmentSnapshot {
	InlineVector<TemplateBindingSnapshot, 4> bindings;
	std::shared_ptr<const TemplateEnvironmentSnapshot> parent;
};
```

Replace repeated node fields:

- `outer_template_param_names_`
- `outer_template_args_`

Candidate files:

- `src/AstNodeTypes_Expr.h`
- `src/AstNodeTypes_Template.h`

Steps:

1. Add snapshot field beside old fields.
2. Populate both old and new fields.
3. Migrate readers to snapshot.
4. Remove old fields only after all readers are gone.

Acceptance criteria:

- lambda template outer bindings still work;
- member templates inside class templates still find outer class args;
- out-of-line member templates still resolve outer args.

Complexity: M  
Risk: Medium

### Phase 5: Make `TypeInfo::InstantiationContext` Binding-Based

Goal: make persistent type metadata record bindings, not parallel arrays.

Change from:

```cpp
InlineVector<StringHandle, 4> param_names;
InlineVector<TemplateArgInfo, 4> param_args;
const InstantiationContext* parent;
```

to:

```cpp
InlineVector<TemplateBindingSnapshot, 4> bindings;
const InstantiationContext* parent;
```

Steps:

1. Add binding-based fields beside old fields.
2. Add accessors that expose bindings first and synthesize old views when needed.
3. Migrate lookup/materialization code to binding accessors.
4. Remove old arrays after migration.

Candidate readers:

- `materializePlaceholderTemplateArgs(...)`
- `resolveDependentMemberAlias(...)`
- dependent base materialization paths in `Parser_Templates_Inst_ClassTemplate.cpp`
- lazy nested type and lazy type alias paths.

Acceptance criteria:

- concrete class template instantiations round-trip their template args through `TypeInfo`;
- dependent placeholders retain enough environment to materialize later;
- nested class templates preserve parent bindings;
- partial-specialization pattern params bind to concrete instantiation args correctly, including variadic packs.

Special attention: commit `8fb3701c` added ad hoc alignment vectors such as `template_args_for_member_copy` and `template_args_for_pattern` in `Parser_Templates_Inst_ClassTemplate.cpp`. The binding-based context must preserve that behavior before those local vectors can be simplified.

Complexity: L  
Risk: High

### Phase 6: Make Persistent Argument Metadata Lossless

Goal: ensure `TemplateTypeArg` can round-trip through persistent metadata.

Extend or replace `TypeInfo::TemplateArgInfo` so it preserves:

- ordered array dimensions;
- template-template argument identity;
- member pointer kind;
- pointer cv qualifiers;
- cv/ref qualifiers;
- function signature;
- dependent name;
- persistent dependent expression where required;
- typed NTTP identity through `NonTypeValueIdentity`.

Tests to add before implementation:

- multidimensional array type template argument round-trip;
- function pointer template argument through `TypeInfo::InstantiationContext`;
- member function pointer template argument round-trip;
- typed bool/int/unsigned NTTP identity.

Complexity: M-L  
Risk: Medium

### Phase 7: Make `TemplateInstantiationKey` Ordered

Goal: make cache/specialization identity match ordered C++ template argument lists.

Do this in stages:

1. Add ordered identity type beside the current key.
2. Log/assert when the old split type/value/template-template key would lose order or collide.
3. Migrate instantiation caches to ordered identity.
4. Migrate exact specialization lookup.
5. Migrate partial-specialization matching inputs while keeping kind-specific views for algorithms that need them.

Sketch:

```cpp
enum class TemplateArgKind : uint8_t {
	Type,
	Value,
	Template
};

struct TemplateArgIdentity {
	TemplateArgKind kind;
	TypeIndexArg type;
	FlashCpp::NonTypeValueIdentity value;
	StringHandle template_name;
};

struct OrderedTemplateInstantiationIdentity {
	StringHandle base_template;
	InlineVector<TemplateArgIdentity, 4> args;
};
```

Tests to add before implementation:

- `template<typename T, int N, template<typename> class C>` mixed ordered args;
- same apparent type/value/template argument set in different source order;
- exact specialization with mixed type/value args;
- partial specialization with mixed ordered args.

Complexity: L  
Risk: High

### Phase 8: Refactor Instantiation Paths Around The Environment

Goal: reduce duplicated function/member template materialization without flattening genuinely different C++ paths.

Keep these front ends separate:

- implicit free function template calls;
- explicit free function template calls;
- implicit member function template calls;
- explicit member function template calls;
- constructor templates;
- class templates;
- variable templates;
- alias templates;
- lazy member/static/nested/type-alias materialization.

Merge only after arguments are bound and an environment exists.

Add helpers:

```cpp
std::optional<ASTNode> instantiateBoundFunctionTemplate(...);
bool finalizeInstantiatedFunction(...);
MaterializedFunctionParameters materializeTemplateFunctionParameters(...);
std::optional<ASTNode> failTemplateInstantiation(...);
```

Initial target:

- unify `try_instantiate_template(...)` and `try_instantiate_template_explicit(...)` after argument binding;
- leave class template instantiation separate;
- leave lazy member instantiation separate but feed it `TemplateEnvironment`.

Example:

```cpp
template <typename T>
T twice(T value) { return value + value; }

int a = twice(21);
int b = twice<int>(21);
```

Both calls should use different binding front ends, then the same materialization/finalization path.

Complexity: L  
Risk: High

## Required Invariants

Add debug assertions as each phase creates the relevant type:

- non-pack bindings contain exactly one argument after defaults are applied;
- pack bindings are the only bindings allowed to contain zero or more than one argument;
- type parameters do not bind value args;
- non-type parameters bind value args, or a dependent value arg before final materialization;
- template-template parameters bind `is_template_template_arg`;
- persistent environment snapshots can materialize back to `TemplateTypeArg`;
- generated instantiation keys are built only from materialized ordered argument lists;
- `HardUse` environments consumed by concrete materialization contain no unresolved dependent non-pack bindings;
- `SfinaeProbe` failures are memoized per overload or candidate, not only per template name and argument list;
- `ShapeOnly` artifacts are never registered as fully materialized functions for code generation;
- body substitution failures for selected templates are never reported as SFINAE unless the caller is explicitly in immediate-context probing;
- placeholders created for dependent types record why they are dependent and which environment can later materialize them.

## Test Plan

Run after each phase:

```powershell
pwsh tests/run_all_tests.ps1
```

Add focused tests as the relevant phase starts:

- class template type args;
- class template NTTP args;
- class template template-template args;
- ordered mixed args: `template<typename T, int N, template<typename> class C>`;
- member function templates inside class templates;
- out-of-line member templates;
- nested class templates;
- nested template arguments such as `Wrap<Pair<T, N>>`;
- user-defined class type arguments;
- function pointer type arguments;
- multidimensional array type arguments;
- variadic class templates;
- variadic function templates;
- defaults depending on earlier template parameters;
- constexpr/static member access through instantiated template contexts;
- SFINAE trailing return failure selects a fallback overload;
- the same missing member in a selected function template body is a hard failure;
- non-dependent lookup inside a template binds at definition time, not call-site time;
- dependent base member alias resolves only after concrete base instantiation;
- shape-only class instantiation later upgrades to full materialization;
- lazy member function uses outer class NTTP plus inner member type parameter;
- lazy static member initializer reports circular dependencies as hard errors;
- instantiated `auto` return cannot reach mangling or codegen unresolved;
- invalid unused template member body is not diagnosed until the member is instantiated;
- invalid selected template member body is diagnosed at point of use.

## Background Reference

### Why This Plan Exists

The compiler repeatedly carries template bindings as adjacent containers:

- `template_params` plus `template_args`
- `template_param_names` plus `template_args`
- `outer_template_param_names_` plus `outer_template_args_`
- `TypeInfo::InstantiationContext::param_names` plus `param_args`
- `OuterTemplateBinding::param_names`, `param_args`, `params`, and `all_args`
- `ConstExpr::EvaluationContext::template_param_names` plus `template_args`
- `ExpressionSubstitutor` maps plus an optional parameter-order vector

This causes positional drift, lossy conversions, duplicated pack handling, and inconsistent fallback behavior.

### Current Strengths

`TemplateTypeArg` is the right working carrier and should remain the canonical in-memory argument form. It already covers type args, non-type values, template-template args, cv/ref/pointer metadata, array metadata, dependent names, dependent NTTP expressions, function signatures, and packs.

`NonTypeValueIdentity` is the right value identity carrier for typed NTTPs.

`TypeInfo::InstantiationContext` points in the right direction because instantiated types should own enough context to support later dependent lookup and lazy materialization.

### C++20 Rules This Work Must Respect

Two-phase lookup:

- non-dependent names bind at template definition time;
- dependent names are looked up at instantiation time;
- current instantiation and dependent base lookup have special rules.

Immediate-context SFINAE:

- return type, parameter types, explicit specifiers, constraints, and template parameter substitution can discard candidates during overload resolution;
- selected function bodies do not get general SFINAE treatment.

Point of instantiation:

- cache identity and point-of-instantiation diagnostics/lookup are related but not the same thing;
- the same template args can need different diagnostic context even if the cached artifact is reused.

Lazy instantiation:

- lazy member/static/nested/type-alias materialization is acceptable when it behaves as on-demand instantiation at the required point of use;
- invalid unused template members should not be diagnosed early;
- invalid selected members must be diagnosed at use.

Dependent placeholders:

- valid while parsing dependent templates or preserving unresolved dependent state;
- not valid as a hard-use recovery path after concrete substitution fails.

## Non-Goals

This plan does not attempt to:

- redesign specialization ranking;
- move all template instantiation out of parser code;
- replace `TemplateTypeArg`;
- remove lazy member instantiation;
- make templates fully C++20-complete in one pass;
- change code generation behavior directly.

## Expected Outcome

Template code should eventually pass around explicit concepts:

- `TemplateArgumentList` for ordered actual arguments;
- `TemplateEnvironment` for parameter binding lookup;
- `TemplateEnvironmentSnapshot` for persistent binding metadata;
- ordered `TemplateInstantiationKey` data for cache/specialization identity.

That should reduce accidental fallback paths, make nested template instantiation less brittle, and make future work on member templates, template-template parameters, NTTP defaults, and dependent constexpr evaluation more predictable.
