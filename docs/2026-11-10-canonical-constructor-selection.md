# Canonical constructor selection service

## Problem

Constructor-candidate matching is duplicated across several callers:

- `tryEvaluateCanonicalRecordConstructibleFromArgs` in `TypeTraitEvaluator.cpp`
- `tryBuildCanonicalUserDefinedClassConversionPlan` in `TypeTraitEvaluator.cpp`
- `tryCanonicalConstructorCallDefaultNoexcept` in `CanonicalTypeAdapter.h`
- `tryCanonicalScalarConvertingConstructorNoexcept` in `CanonicalTypeAdapter.h`
- `tryHasCanonicalImplicitConvertingConstructorForArgument` in `OverloadResolution.h`
- the compatibility `resolve_constructor_overload` path

Each copy has slightly different arity, explicit/access, copy/move, ellipsis, default, and unknown-handling rules. The resulting drift caused copy/move constructors to be treated as converting constructors, inconsistent omitted-default handling, and conditional class-parameter import.

## Contract

One canonical selection service answers: *which constructor, if any, accepts these arguments?*

- Input: `CanonicalTypeTable&`, target class `TypeId`, argument views (`TypeId`, lvalue flag, optional expression-exception fact), and a policy.
- Output: tri-state status (`Selected`, `NoMatch`, `Unknown`), selected constructor index, conversion ranks, whether the selected constructor is a copy/move constructor, its `noexcept` fact, and the omitted-default exception summary.
- Unknown always propagates: a definite match is never returned when a candidate is undecidable.

## Policies

- `AllowExplicit`: direct-initialization allows explicit constructors; implicit conversion does not.
- `AllowUserDefinedArgumentConversion`: constructibility allows a converting constructor for a class argument; nested default analysis allows one scalar-to-class hop.
- `AllowEllipsis`: extra arguments may use the ellipsis conversion sequence.
- `IncludeCopyMoveConstructors`: normal construction includes them; converting-constructor lookup excludes them.
- `RequireCompleteSchema`: any incomplete schema defers unless a later compositional-schema slice narrows it.
- `LiteralArgumentsOnly`: default-exception analysis only classifies scalar literal/call arguments.
- `ExistenceOnly`: reports a definite viable candidate even when another candidate is undecidable; used by viability probes, not overload selection.

## Migration order

1. Add `src/CanonicalConstructorSelection.h` with the policy, argument view, result, and single selection loop.
2. Migrate `tryEvaluateCanonicalRecordConstructibleFromArgs` to the service.
3. Migrate `tryBuildCanonicalUserDefinedClassConversionPlan` to the service with implicit-conversion policy.
4. Move the default-exception helpers out of `CanonicalTypeAdapter.h` into a `.cpp` that can include `OverloadResolution.h`, then use the service with literal/call arguments.
5. Add a compact per-default exception classification (nothrow / known-throwing / unknown) to the constructor schema and remove `nothrow_default_count` / `last_known_throwing_default` once all readers migrate.
6. Use the same service for the `OverloadResolution.h` implicit converting-constructor probe; the compatibility path remains.

## Status

The constructor-selection service is implemented and the argument-bearing
constructibility query uses it. Default-exception fact evaluation now lives in
`TypeTraitEvaluator.cpp` behind `CanonicalDefaultFacts.h`, so it can include the
service without pulling `OverloadResolution.h` into `CanonicalTypeAdapter.h`.
Scalar-to-class default conversion already selects through the service.

The constructor schema now stores a compact 2-bit per-default exception
classification (nothrow / known-throwing / unknown) relative to
`minimum_parameter_count`, replacing `nothrow_default_count` /
`last_known_throwing_default`. The publisher, selection service, and call-default
evaluator all read the per-default facts.

The constructor-call default evaluator now selects through the service with the
`LiteralArgumentsOnly` policy and one scalar-to-class hop. Known-throwing and
unknown candidates propagate through the service result instead of a separate
candidate loop.

Scalar and enum arguments now build the same explicit user-defined conversion
plan as class arguments. Copy/move constructors are ranked through that plan
instead of being dropped by a one-argument shape guard, so a direct scalar
constructor wins naturally.

The `OverloadResolution.h` implicit converting-constructor probe now selects
through the service with `ExistenceOnly`. An integer literal zero is passed as a
null-pointer-constant argument fact, so a pointer parameter receives the
`nullptr_t` conversion only where the language rule applies.

Selection results now carry a reason code for missing, empty, or incomplete
schemas, undecidable or ambiguous candidates, and outright no-match results. A
native selection matrix covers those outcomes across the policy combinations.

Constructor parameter publication now uses one import policy: non-implicit
constructors may use the class fallback, while implicit constructors preserve
owner identity and synthesize a self reference when strict import fails. This
removes the default/non-default and implicit/non-implicit import drift.

Constructor schema entries now carry a per-candidate status: complete,
unimportable, conditional, or deleted. Non-complete entries publish an arity
range and no parameter types, so the service only defers when that range can
match, and deleted candidates are non-viable instead of poisoning the schema.
`RequireCompleteSchema` is scoped to matching arities as well.

The canonical table now owns a transactional, immutable expression-exception
fact table keyed by `ExprId`; a missing fact means Unknown. TypeId-level
exception facts remain in the record-property schema.

Remaining: continue with the remaining architecture items below.

## Remaining architecture items

- Publish and consume expression exception facts from default and operator analysis.
- Member-function and conversion-operator schemas so operator lookup and user-defined conversion share one source of truth.
