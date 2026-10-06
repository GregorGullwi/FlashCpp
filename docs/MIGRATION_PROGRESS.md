# Front-end migration progress

This is the handoff for agents continuing the migration. The [rearchitecture
plan](2026-08-24-front-end-rearchitecture-plan.md) is authoritative for the
design, boundaries, and exit criteria. This file records current state and
next work; completed implementation history belongs in git.

Last updated: 2026-10-06.

## Current state

Boundary **3A (canonical types)** is active and incomplete. `TypeSpecifierNode`
now carries an outermost-to-innermost `DeclaratorComponent` spine. Named and
abstract pointer/array declarators use an explicit frame stack, preserving
mixed forms such as `int (*(*p)[3])[4]`. Legacy pointer/array fields are
projected only when the shape is exactly representable.

`CanonicalTypeTable` interns and substitutes structural types, including
ordered pointer/array wrappers and one function component. Descriptors whose
shape needs the structural spine compare by `TypeId`; projectable types still
use flat fields in many semantic operations. Static-member parser lookup uses
the published `TypeId` through one adapter, while sema materializes its flat
projection where legacy conversion code still needs it. Standard conversion
annotation now uses a shared structural planner for supported canonical
imports, while compatibility paths still materialize flat types for unmigrated
families. Parser-side overload ranking and other syntax-facing conversion
callers still use `TypeSpecifierNode`. Ordered pointer objects use
`runtime_pointer_depth`; `__is_same` compares canonical `TypeId`s for supported
operands, importing each operand through the structural-trait importer so a
class-template specialization projected as a nominal specifier recovers its
published EntityId (see
`tests/test_canonical_lazy_same_specialization_concept_ret0.cpp`), and the
shared type-trait evaluator now classifies the whole
structural `[meta.unary.prop]` family from the canonical node: references,
pointers, arrays, functions, member pointers, enums, and the builtin
arithmetic/scalar/fundamental/object/compound groupings derived from them.
Shared evaluation, constant-expression evaluation, and code-generation trait
lowering all route through that one classification, so a trait no longer
answers differently depending on whether it is folded or lowered. Class, union,
cv, signedness, and the `__is_polymorphic`, `__is_final`, and
`__is_abstract` traits now read canonical type identity or published facts for
completed records, including materialized class-template specializations.
`__is_empty`, triviality, lifetime, and constructibility traits still read
sema-owned record metadata, and template, constexpr, and IR consumers still
read flat fields. Array and callable outer wrappers remain guarded where their
consumers are not migrated.

The lazy constraint evaluator routes the same family through that shared
classification. A concept requirement whose operand is a substituted template
parameter is projected onto a declarator specifier at that one boundary, its
nominal `EntityId` is published, and the trait is answered from the canonical
node. A trait the canonical table does not own is now an explicit
`ConstraintSatisfaction::Unknown` outcome instead of a silent success: it is
counted, it is not proof of satisfaction, and `!`, `&&`, and `||` propagate it
rather than resolving it. That removes a real defect - `!__is_trivially_copyable(T)`
in a concept requirement rejected every candidate, because the unclassified trait
answered "satisfied" and `!` inverted it into a hard failure. The regression
`tests/test_canonical_lazy_constraint_traits_ret0.cpp` checks acceptance and
rejection for the classified family over native scalars, records, both enum
forms, member pointers, an array reference, and a class-template member, plus
the three unknown-outcome shapes. The new `lazy_constraint_trait_fallback`
counter has a fixed corpus and baseline and must reach zero at the 3A exit; the
existing concept corpus already measures zero, so the residual six come from the
regression's own unclassified-trait probes.

A type-trait operand is a declarator specifier the parser materializes, and it
now publishes its nominal `EntityId` at that point rather than being re-stamped
by whichever consumer needs identity. The two materialization sites are the
decltype builder and the trait's own type-id parse, so a bare type-id operand
and a `decltype` operand of one type carry the same identity.
`canonical_structural_trait_fallback` is therefore zero on
`tests/test_canonical_structural_type_traits_ret0.cpp` and on
`tests/test_canonical_nominal_trait_operand_entity_ret0.cpp`, whose baseline was
lowered from 23 to 0 so any reappearance fails the counter run. The trait answers
themselves are unchanged: the compatibility classifier agreed on every shape in
that corpus, so this is an authority migration, and the counter is the
machine-checkable evidence rather than a behavioral difference. The regression
covers both operand spellings, a union, a class-template specialization, and
`__is_same` between distinct records and between both enum forms.

The lazy-constraint operand materializer now publishes a member pointer's
declaring-class identity alongside the record/enum nominal entity, so a
member-object-pointer operand is classified canonically instead of remaining an
unclassified lazy-constraint trait. `tests/test_canonical_member_object_pointer_operand_identity_ret0.cpp`
baselines both trait-fallback counters at 0; on the lazy regression
`canonical_structural_trait_fallback` fell from 2 to 0 and
`lazy_constraint_trait_fallback` from 6 to 4.

The class, union, and qualification traits are answered from the canonical type
as well. A record's class/union split comes from its published
`CanonicalRecordLayout` union flag, a class-template specialization is a class
type and never a union, and a record with no published layout fails closed to the
compatibility classifier rather than guessing. The
canonical record-property schema publishes unary property facts keyed by class
`TypeId`, including polymorphic, final, abstract, triviality, layout, aggregate,
empty, and destructor properties; the shared evaluator and lazy constraints use
those facts. cv qualification comes from the canonical qualifier plus
[dcl.array]'s rule that an array is identically
cv-qualified to its element, walked iteratively so array rank stays off the
native stack, and [dcl.ref]'s rule that cv introduced through a reference is
dropped. Signedness is decided once, from the canonical builtin, and includes the
floating-point types an integer-only reading of the question would miss; plain
`char` is signed on this target and the wide character type follows the data
model. `long double` is deliberately absent from that coverage: the shipped model
gives it the `double` size and representation on every target and the backend
cannot compute with it, so a signedness assertion about it would only restate
the `double` one. See the x87 known issue. The flat `TypeTraitEval::isSigned` and `isUnsigned` predicates no longer
carry that policy - they project a category onto the canonical builtin that
decides it and defer, so the compatibility path cannot restate it, and the
projection is deliberately not the inverse of `canonicalBuiltinToTypeCategory`
because the signed/unsigned question only needs the unambiguous categories. This fixes six constant-expression answers that disagreed
with code generation: an array, a const array, a `double`, and a `long double`
were reported signed, and cv on a pointer object was missed while cv introduced
through a reference was reported as qualifying the reference.
`tests/test_canonical_class_qualification_traits_ret0.cpp` covers the family over
plain and signed and unsigned builtins, `wchar_t`, `char`, floating types, both
enum forms, a union, a struct, a derived struct, a const record, a
class-template specialization, arrays of every bound shape, const and volatile
qualifiers, references, and a const pointer, in both the folded and the lowered
path. The unary record-property family is now published as TypeId-keyed facts
for completed records. This includes triviality, standard layout, aggregate and
empty status, destructor properties, and the existing polymorphic, final, and
abstract properties. Constructibility remains sema-owned because its answer
depends on a variadic argument list and overload resolution rather than a unary
property of a completed record.

Nominal identity is now a property of the syntax node rather than of whichever
consumer reads it: a record or enum specifier carries its `EntityId` from the
moment the parser materializes it, so every importer sees it. That includes the
consumers that take a `const TypeSpecifierNode&` and so cannot stamp it
themselves - the trait evaluator's three entry points, and the
`OverloadResolution.h` same-type, qualification, and conversion-planner
tie-breakers. A const-ref signature is therefore not evidence that an operand
lacks its identity; remaining work item 2 says how to confirm a real gap.

Overload-ranking tie-breakers for reference parameter identity and pointer
qualification now import supported syntax types and compare canonical `TypeId`
structure, preserving nested declarators, array bounds, and pointee cv. Types the
canonical importer does not yet support retain the compatibility tie-breakers.

Semantic conversion support covers ordered shape identity, pointer
qualification, object-pointer-to-`cv void*`, array/function decay, boolean
conversion, and outermost ordered-reference binding. Function signatures retain
non-projectable declarator spines, and function decay compares the complete
callable type including ordered returns. Template-signature substitution now
composes substituted pointer and array wrappers with retained callable
declarator spines before canonical import. Derived-to-base conversions through
non-projectable declarators and callable-component conversions remain deferred.
For dereferences whose result has an unprojectable
ordered declarator, parser typing leaves the expression
unresolved and defers overload selection to sema. Sema uses the canonical
argument type for selection and reports ambiguous or non-viable calls at the
call site. Other parse-time expression queries still use the compatibility
type view where needed.
When a function-template address matches a free function-pointer parameter,
overload probes remain non-instantiating; after the consuming overload wins,
the parser materializes the deduced specialization and records it on the
address expression. Sema and IR then use that declaration, including for
overloaded templates where the target function signature selects a more
specialized parameter pattern. The regression
`tests/test_free_function_template_address_materialization_ret0.cpp` checks
runtime invocation through both a direct template address and an overloaded
template address.
The same contextual-resolution record now covers member-function addresses:
sema attaches the selected member specialization to `&Owner::member`, and IR
lowers it to the member's code symbol with the member-function-pointer IR type.
Instantiation is deferred until IR so a parser-owned pattern root never reaches
the late-materialization boundary. The regression
`tests/test_member_function_pointer_address_materialization_ret0.cpp` checks an
implicitly converted address, a `static_cast` address, and the deduplicated
member-template overload, plus that a null member pointer stays null. Virtual
member addresses and multiple-inheritance adjustment remain unsupported (see
`KNOWN_ISSUES.md`).
Conditional pointer common-type selection now compares imported structural
`TypeId`s through the shared descriptor adapter. Derived-to-base pointer
conversions now classify direct record pointees from canonical base schemas
keyed by `EntityId`, preserving public unique and virtual bases while rejecting
inaccessible, ambiguous, and cv-removing conversions. Imported pointer-pair
no-matches are authoritative, so derived-to-base ranking does not fall through
to the compatibility `TypeIndex` classifier.
Parser-side overload ranking now uses the structural planner for non-projectable
ordered pairs plus scalar builtin conversions, `nullptr`-to-pointer conversion,
and supported projectable pointer pairs, including derived-to-base conversion,
array decay, and pointer/array-to-`bool` conversions. Reference binding also
uses canonical plans for `bool` conversion temporaries from pointer, array,
function, and member-pointer sources. Eligible const lvalue and rvalue
references bind; mutable lvalue references remain non-viable. The regression
`tests/test_canonical_pointer_to_bool_reference_temporary_overload_ret0.cpp`
checks that object pointers, arrays, function pointers, and member pointers
prefer the materialized `bool` reference over an ellipsis.
Same-shape reference binding, including exact-shape prvalues that materialize
for `const` lvalue references, now plans from canonical `TypeId`s
while carrying expression value category separately. Rvalue binding retains
exact-match rank when it adds top-level cv. Imports stay within builtin, record,
or enum base types. Speculative imports roll back; standard builtin-to-builtin
conversions can now bind eligible references through a temporary, such as an
`int` value converted to `double` for `const double&`; an lvalue converted to
`double` can also bind to `double&&`. Array lvalues now decay canonically when
binding pointer temporaries to eligible const lvalue and rvalue references.
Fixed-underlying unscoped enum conversions to arithmetic parameters now use
canonical enum layout metadata: conversion to the declared underlying type and
to its promoted integral type are both integral promotions. When those targets
differ, overload ranking prefers the declared underlying type; other arithmetic
targets are conversions. Scoped enum arguments retain their existing diagnostic
path because reporting the
scoped-enum-specific error requires overload selection to complete. Unfixed
unscoped enums now publish their promotion target as a canonical `TypeId`,
selected from the enum's full minimal-width value range; overload ranking uses
that target for integral promotion and treats other arithmetic destinations as
conversions. The chosen type also becomes the parser's implementation-defined
underlying type, keeping enum layout and `sizeof` consistent with that range.
The regression
`tests/test_canonical_fixed_enum_promotion_overload_ret0.cpp` checks overload
selection for fixed narrow and wide underlying types, an opaque fixed enum used
before its definition, and ordinary enums, while
`tests/test_scoped_enum_call_arg_e1401.cpp` guards the scoped-enum diagnostic.
The migration counter guards fixed and unfixed unscoped enums from returning to
TypeIndex fallback ranking. The regression
`tests/test_canonical_unfixed_enum_promotion_overload_ret0.cpp` checks `int`,
`unsigned int`, and platform-width-sensitive signed promotions for by-value and
reference parameters. The regression
`tests/test_canonical_fixed_enum_promoted_underlying_ranking_ret0.cpp` checks
the promoted-underlying rank, the preference for the underlying type, and both
rules through value and reference parameters.
Eligible `const` lvalue and rvalue references can now bind through the same
fixed-enum arithmetic conversions using the canonical plan; a mutable lvalue
reference still cannot bind to the converted temporary. The regression
`tests/test_canonical_fixed_enum_reference_temporary_overload_ret0.cpp` checks
underlying-type ranking from an enum lvalue and rejects the mutable-reference
candidate, while `tests/test_scoped_enum_reference_call_arg_e1401.cpp` preserves
the scoped-enum diagnostic for a reference parameter. The regression
`tests/test_canonical_unfixed_enum_reference_temporary_overload_ret0.cpp` also
checks promotion-ranked const-reference binding from an unfixed enum lvalue,
rvalue-reference binding from an enum prvalue, and rejection of a mutable
lvalue-reference candidate; its fixed-corpus row keeps the enum TypeIndex
fallback at zero.
At ordinary calls, an array lvalue can bind to `int*&&` through that converted
pointer temporary, as covered by
`tests/test_array_decay_rvalue_reference_temporary_ret0.cpp`.
Nested array extents and element cv are retained through decay, and mismatched
pointer-to-array extents are rejected. Exact-shape array references still
preserve extents and nested cv without decay. Derived-record to base-reference
conversions classify accessibility and ambiguity by traversing canonical base
schemas keyed by `EntityId`; overload planning no longer round-trips those
relationships through compatibility `TypeIndex`s. Derived-to-base pointer
conversions now use the same canonical base graph for direct record pointees.
Null-pointer conversions to object pointers and data-member pointers now bind
eligible const lvalue and rvalue references through canonical temporary
conversion plans; non-const lvalue references remain non-viable. The adapter
preserves the top-level cv of flat member-pointer declarators and removes the
outer reference before importing their pointee. Unit coverage also checks
function-pointer and member-function-pointer targets through canonical type
imports. The source regression
`tests/test_canonical_nullptr_reference_temporary_overload_ret0.cpp` verifies
overload selection for object and data-member pointers. Function-pointer and
member-function-pointer reference declarators now parse through the canonical
conversion path. The regression
`tests/test_canonical_callable_pointer_nullptr_reference_temporary_overload_ret0.cpp`
checks that `nullptr` selects a `const` function-pointer reference over a
non-const lvalue reference and materializes a null member-function pointer for
a local const-reference binding.
Function designators now decay canonically when a matching function-pointer
temporary binds to a `const` lvalue or rvalue reference. The planner rejects
non-const lvalue-reference binding and mismatched function signatures. Parser
typing also unwraps the legacy implicit `FunctionPointer` category when unary
`*` produces a function lvalue, so overload selection sees the function type
before decay. The unit coverage is in
`Canonical TypeIds bind function decay temporaries to pointer references`, and
`tests/test_canonical_function_pointer_reference_decay_overload_ret0.cpp`
checks const-lvalue and rvalue-reference overload selection from a dereferenced
function pointer.
Regular function-pointer parameter pairs now compare canonical function
`TypeId`s, preserving exact signatures and accepting the standard conversion
from `noexcept` to potentially-throwing pointers. Reverse `noexcept` conversion
and mismatched return or parameter types are rejected; top-level cv on
by-value pointer arguments is ignored. The unit case
`Canonical TypeIds compare projectable function pointer pairs` and
`tests/test_canonical_function_pointer_noexcept_pair_overload_ret0.cpp`
cover ranking and viability. A dependent function-pointer argument in a
function-template call now has source coverage showing that overload selection
waits for substitution: `noexcept(true)` selects the non-throwing overload,
`noexcept(false)` selects the throwing overload, and both convert to a
throwing-only parameter. This is checked by
`tests/test_canonical_dependent_noexcept_deferred_overload_ret0.cpp`.
An added top-level cv-qualifier introduced purely by reference binding is now
ranked as the identity conversion per [over.ics.ref]/1 instead of as a
`QualificationAdjustment`. A direct `const T&` binding of a function-pointer
lvalue therefore outranks binding to a `const&` of the potentially-throwing
pointer, which still requires the [conv.fctptr] function pointer conversion
(category Qualification Adjustment, exact-match rank). The preference between
`T&` and `const T&` for an lvalue is applied by the [over.ics.rank]/3.2.6
cv-preference tie-break when comparing candidate sequences; the helper that
compares referenced types ignoring top-level cv now also ignores the outermost
pointer level's cv. Array references follow the same rule: a referenced array
that differs only by top-level element cv is an identity binding, so
`int (&)[N]` is preferred over `const int (&)[N]`, while element types that
require a structural qualification (for example an array of pointers gaining
pointee cv) remain qualification conversions. The unit case
`Canonical TypeIds bind function pointer conversions to references` and the
source regressions
`tests/test_canonical_noexcept_function_pointer_reference_temp_overload_ret0.cpp`
and `tests/test_reference_binding_cv_ranking_ret0.cpp` lock in the selection.
Same-owner member-function-pointer pairs now compare the canonical owner and
full function type, accept `noexcept` relaxation, and reject reverse
relaxation or signature mismatches. Complete owner schemas reject unrelated,
inaccessible, ambiguous, and virtual-base owner conversions. Member-function-
pointer pairs also support public unambiguous non-virtual base-to-derived owner
conversion through canonical `TypeId`s, including `noexcept` relaxation, and
reject mismatched function signatures. Identical context-local dependent
`noexcept` expression IDs now remain comparable as canonical function identity;
distinct or one-sided dependent expressions still defer until substitution.
Unit coverage checks both paths, while source regressions cover dependent
`noexcept` overload selection for regular and member-function pointers.
MSVC x64 function-pointer signatures now preserve the ABI distinction between
`__vectorcall` and the default calling convention in nested type mangling, so
overloads accepting those callback types remain separate declarations. The
regression `tests/test_canonical_function_pointer_calling_convention_overload_ret0.cpp`
checks that each callback selects its matching overload.
Member declarations now carry the same convention into generated member-
function-pointer types, and canonical imports recover published nominal
parameter identity before comparing those signatures. The regression also
checks `decltype(&Owner::method)` overloads with struct parameters.
Dependent `decltype(&Callable<T>::member)` aliases now recover the substituted
member-function signature and canonical owner before overload ranking.
Nested class-template arguments in those owners now import recursively from
their published type metadata, and member-function address formation binds the
exact declaring class as a canonical `TypeId`. The regression
`tests/test_canonical_dependent_member_pointer_alias_chain_overload_ret0.cpp`
checks three forwarding aliases over a `Callable` owner with sixteen nested
`ValueBox` specializations, selecting the `noexcept` or potentially-throwing
overload after substitution for `int` and `char`.
`tests/test_canonical_distinct_dependent_member_noexcept_pair_overload_ret0.cpp`
checks that distinct dependent exception expressions select the non-throwing
target for `int` and reject it for `char` when the source member is potentially
throwing.
`tests/test_canonical_one_sided_dependent_member_noexcept_pair_overload_ret0.cpp`
checks a dependent source exception specification against fixed non-throwing
and potentially-throwing target aliases after class-template substitution.
Deferred ordinary function-template addresses now retain their explicit
template arguments through substitution, so `decltype(&function<T>)` aliases
use substituted signatures for overload ranking. A global function-template
address reached through an alias-template specialization now types as the same
flat callable specifier as the directly written address. Alias substitution
re-shapes the address-of operand into a qualified identifier, and the
address-of query now returns the designator's function-pointer type instead of
adding a second pointer level. Canonical identity already agreed through
`TypeId`; this removes the flat template-argument divergence, so
`SameType<Type, Type>` and `__is_same` agree. The regression
`tests/test_canonical_alias_function_template_address_identity_ret0.cpp`
checks alias-versus-direct identity for zero-parameter, scalar-parameter, and
record-parameter function templates.
`tests/test_canonical_distinct_dependent_noexcept_function_pointer_alias_overload_ret0.cpp`
checks that distinct dependent exception expressions select the non-throwing
target for `int` and the potentially-throwing target for `char`, and resolves a
32-level nested function-address alias chain.
Deferred member-function `decltype` aliases now publish the resolved callable
specifier as well as the legacy type projection, preserving the substituted
owner and exception specification for overload ranking.
`tests/test_canonical_dependent_member_pointer_owner_alias_overload_ret0.cpp`
checks the class-template owner form, and
`tests/test_canonical_dependent_member_pointer_template_owner_alias_overload_ret0.cpp`
checks a dependent owner type parameter. The regression
`tests/test_canonical_dependent_member_template_pointer_ranking_ret0.cpp`
covers an explicitly selected member-function-template address; all three
verify `noexcept`-sensitive selection after substitution.
Data-member-pointer pairs now
compare canonical owner and pointee types, preserve same-owner qualification,
and allow a public unambiguous non-virtual base-to-derived owner conversion.
Complete owner schemas reject unrelated, inaccessible, ambiguous, and
virtual-base conversions. Member-pointer owner binding gets its canonical
table through `requireFrontendContext()` and fails fast outside a front-end
context. Function declaration matching also retains the member-owner
`EntityId`, so overloads with distinct data-member-pointer owners remain
separate candidates. The unit case
`Canonical TypeIds compare same-owner member function pointer pairs` checks
signature ranking. The unit case
`Canonical TypeIds compare member object pointer pairs` checks exact owner,
qualification, and owner-mismatch behavior. The source regression
`tests/test_canonical_member_object_pointer_pair_overload_ret0.cpp` checks
pointee and owner selection, including base-to-derived ranking. The source
regression
`tests/test_canonical_member_function_pointer_pair_overload_ret0.cpp` checks
member-owner, function-signature, and base-to-derived selection on both MSVC
and Itanium. Itanium mangling covers member-function-pointer parameter types,
including qualified and `noexcept` signatures, with ABI substitution
compression. Nested function-pointer parameter pairs such as `int (*)(int)`
versus `int (**)(int)` now rank through distinct flat `FunctionPointer`
identities rather than collapsing. Address-of expressions for non-static data
members (`&C::m`) now form parser-facing member-object-pointer types whose
owner is the declaring class, so Exact Match to that owner ranks ahead of
[conv.mem] base-to-derived owner conversion; inherited members keep the
declaring-class owner rather than the naming class. The regression
`tests/test_canonical_member_object_pointer_address_of_exact_vs_base_conversion_ret0.cpp`
checks `&Base::value`, `&Derived::value`, and `&Derived::own` selection.
Member-object-pointer owners spelled as class-template specializations such as
`Holder<int>::*` now publish specialization `TypeId` identity at parse
(spelling remains only a lexical projection); `tests/test_canonical_template_id_member_pointer_owner_overload_ret0.cpp`
checks parameter and alias overload selection. Member-function-pointer owners
spelled the same way (`int (Holder<int>::*)()`) now take the structural
declarator path for template-id owners and publish the same specialization
`TypeId` authority; `tests/test_canonical_template_id_member_function_pointer_owner_overload_ret0.cpp`
checks parameter and address-of overload selection with identical signatures
that differ only by owner. Using-alias spellings of member-function and
member-object pointers now project owner `TypeId`/`EntityId` through
`resolveAliasTypeInfo`, so `static_cast<Alias>(nullptr)` ranks by owner identity
even when flat `type_index` values collide; covered by
`tests/test_canonical_member_pointer_alias_owner_typeid_overload_ret0.cpp`.
Alias-template targets that are member-object or member-function pointers with
class-template owners (`using FieldPointer = int Holder<Type>::*`,
`using RunPointer = int (Holder<Type>::*)()`) now parse as real mop/MFP shapes
and rematerialize the owner specialization through alias parameter bindings so
`FieldPointer<int>` / `RunPointer<char>` publish distinct owner `TypeId`
identity for cast overload ranking; covered by
`tests/test_canonical_alias_template_member_pointer_owner_overload_ret0.cpp`
(class-template owners and type-parameter owners). Alias-target substitution
preserves member-pointer owner identity through the shared outer-modifier path
and republishes concrete owners via `applyMemberPointerOwner` / TypeIndex import
rather than spelling-only name-map binding.
Typedef spellings of member-function pointers now parse through the shared
declarator path (plain and template-id owners), so
`static_cast<TypedefName>(nullptr)` ranks by owner identity; covered by
`tests/test_canonical_typedef_member_function_pointer_owner_overload_ret0.cpp`.
Class-scope typedef and nested-name mop/MFP aliases (`Typedefs::Field`,
`Typedefs::Run`) now publish struct-relative TypeInfo spellings and project
owner `TypeId` through nested alias materialization
(`resolveTypeInfoToTypeSpec` / `buildTypeFromInfo`); covered by
`tests/test_canonical_class_scope_typedef_member_pointer_owner_overload_ret0.cpp`.
Dependent `noexcept`, standard-conversion tails outside the canonical
projectable set, and other unsupported callable or template types still use
compatibility planning. Calling-convention-sensitive
regular and member-function-pointer ranking now preserves the signature through
overload selection; `tests/test_canonical_function_pointer_calling_convention_overload_ret0.cpp`
covers both forms. Parenthesized function-pointer declarators now carry their
parsed calling convention into the constructed `FunctionSignature`, including
named callback parameters with dependent parameter types. This lets canonical
ranking distinguish `__vectorcall` from `__cdecl` after substitution. The
regression `tests/test_canonical_dependent_calling_convention_function_pointer_overload_ret0.cpp`
checks the declared parameter type and overload selection for both conventions
after substituting `Packet`.
Dependent member-function-pointer declarators now retain their parsed calling
convention too. Canonical import binds published nominal entities inside
structured `FunctionType` components after substitution, so an lvalue callback
selects the matching `__vectorcall` or `__cdecl` overload instead of falling
back to category-only ranking. The regression
`tests/test_canonical_dependent_member_calling_convention_overload_ret0.cpp`
checks distinct member-function-pointer types, direct lvalue selection, and
overload selection after substituting `CallbackOwner<Packet>`.
Member-function declarations now retain their `&` and `&&` qualifiers through
AST copies and deferred template-member replay. Address formation includes the
qualifier in the published member-function-pointer signature, and out-of-line
declaration matching distinguishes the ref-qualified overloads. The regression
`tests/test_canonical_member_function_pointer_ref_qualifier_overload_ret0.cpp`
checks that `decltype(&Owner::method)` matches an explicitly ref-qualified
member-function-pointer alias and selects its matching overload.
Class-to-scalar conversion-function sequences now retain the selected
conversion function and its trailing standard conversion through overload
ranking, sema, and call lowering. The trailing conversion rank breaks ties
between user-defined sequences, and the call uses the conversion function's
declared result type before applying that standard tail. Regressions
`tests/test_conversion_operator_trailing_standard_sequence_ret0.cpp` and
`tests/test_conversion_operator_trailing_standard_rank_ret0.cpp` cover
`operator int()` followed by `int`-to-`float` conversion and promotion-versus-
conversion ranking. `tests/test_conversion_operator_trailing_standard_return_ret0.cpp`
checks return lowering, while
`tests/test_conversion_operator_trailing_standard_assignment_ret0.cpp`
checks local and global assignment lowering. This covers scalar tails accepted
by the canonical projectable planner; unsupported tail families remain on
compatibility paths.
Pointer-valued conversion functions can also use the standard pointer-to-`bool`
tail; `tests/test_conversion_operator_pointer_bool_tail_ret0.cpp` checks both
copy-initialization and overload selection against ellipsis, and the selected
call preserves its pointer depth for boolean lowering.
`FunctionDeclarationNode` does not yet retain an `explicit` specifier for
conversion functions, so implicit viability of explicit conversion functions
remains a separate parser/sema gap.
Constructor-based conversion viability now receives the argument expression:
pointer-taking constructors accept an integer literal zero as a null pointer
constant and reject other integral arguments. Direct-initialization
(`Type obj(args)`) resolves against the same expression-aware conversion, so a
nonzero integer literal fails with the no-match constructor diagnostic while a
zero literal binds the pointer. Regressions
`tests/test_operator_constructor_null_pointer_argument_ret0.cpp` and
`tests/test_operator_constructor_nonzero_pointer_argument_e1319.cpp` cover
binary-operator ranking; `tests/test_function_constructor_nonzero_pointer_argument_e1704.cpp`
checks ordinary-call rejection, and
`tests/test_direct_constructor_nonzero_pointer_argument_e1508.cpp` checks the
direct-initialization no-match. Constructor argument types that are still
dependent (no concrete type index) keep the unique-arity recovery instead of
producing a spurious no-match.
Binary operator-template ranking now sends distinct record operand and
parameter types through the shared conversion planner instead of rejecting
them by `TypeIndex` inequality. The regression
`tests/test_canonical_binary_operator_template_user_defined_conversion_ret0.cpp`
checks that a candidate requiring one converting constructor beats a
candidate requiring two.
Call lowering now materializes the pointer object required when an array
decays to a pointer temporary before binding it to an eligible reference.
`tests/test_array_to_pointer_reference_temporary_ret42.cpp` checks runtime
dereferencing through references to decayed `int*` and `short*` values, while
`tests/test_canonical_array_decay_reference_overload_ret0.cpp` checks overload
selection across builtin and record element types.
Regression coverage in
`tests/test_canonical_prvalue_const_reference_overload_ret0.cpp` exercises
native, record, substituted, and conversion-required reference parameters, and
checks that a promotion-ranked overload beats a conversion-ranked reference.
`tests/test_canonical_array_reference_binding_ret0.cpp` checks array overload
selection for exact extents and mutable versus const referents.
`tests/test_canonical_derived_to_base_reference_overload_ret0.cpp` checks that
derived-reference candidates rank ahead of their base-reference overloads.
`tests/test_canonical_derived_to_base_pointer_overload_ret0.cpp` checks direct
and cv-qualified derived-pointer ranking, including preference for the exact
derived pointer and the less-qualified base pointer when both require
derived-to-base conversion. The canonical planner unit test covers virtual,
inaccessible, ambiguous, and cv-removing base-pointer conversions, and rejects
derived-to-base conversions through pointer-to-pointer or pointer-to-array
shapes. By-value record conversions now compare canonical record identities and
rank a public derived-to-base conversion as a standard conversion. This keeps
it better than a competing converting-constructor sequence, as covered by
`tests/test_canonical_derived_to_base_value_overload_ret0.cpp`. Object-pointer
conversions that create a pointer temporary can now bind
to eligible const lvalue and rvalue references through the projectable
canonical planner, including derived-to-base and object-pointer-to-`cv void*`
conversions; non-const lvalue references still reject that temporary path. The
unit case `Canonical TypeIds bind pointer conversion temporaries to references`
checks those plans, and
`tests/test_canonical_pointer_conversion_reference_overload_ret0.cpp` checks
compile-time overload selection. Reference binding also distinguishes cv on
the pointer object from cv on its pointee: `int*` cannot bind through a
qualification temporary to `const int*&`, while `const int* const&` can accept
it. The unit case `Canonical TypeIds reject pointee qualification through
mutable references` and
`tests/test_canonical_pointer_cv_reference_binding_overload_ret0.cpp` cover
that distinction. Member-object and member-function pointer conversions now
also bind through eligible reference temporaries using canonical owner schemas.
The unit case `Canonical TypeIds bind member-pointer conversion temporaries to
references` covers both pointer-to-member families, and
`tests/test_canonical_member_object_pointer_reference_temp_overload_ret0.cpp`
checks source-level overload selection for a base-to-derived data-member-pointer
conversion. Function-template parameter materialization now republishes the
canonical owner `TypeId` for substituted member-function-pointer types whose
owner specifier did not carry one before substitution. The regression
`tests/test_canonical_member_function_pointer_reference_temp_overload_ret0.cpp`
checks exact `Base<T>` owner ranking against conversion to `Derived<T>::*` and
binding the converted temporary to a const reference after substitution, for
both `int` and record signatures. `tests/test_canonical_array_decay_reference_overload_ret0.cpp`
checks array-to-pointer temporary binding for const lvalue and rvalue references
across builtin and record element types; the canonical planner unit test checks
multidimensional row extents, cv addition/removal, and direct versus temporary
rvalue-reference binding.

Static-member `TypeId`s are recomputed after template substitution when the
canonical importer supports the substituted type, including projectable
pointer-to-array types. Other projectable types retain their substituted flat
projection until their importer family is available. Qualified lookup and
object-member access read a published identity through the same adapter.
Callable aliases can now be imported as the base of ordered pointer/array
declarators, including static members whose type is a pointer to an array of
function pointers, as well as nested callable aliases. Static-member copy and
substitution routes have been audited: direct semantic copies retain their
`TypeId`, substituted members re-import from their substituted declaration,
explicit-specialization copies reuse an already published exact identity when
needed, and the lazy fallback carries the substituted identity with its
registry entry. AST-only `StaticMemberDecl` copies retain the declaration type
and rebuild the semantic identity when materialized. Parser-side materialization
can retain the declared callable signature when the canonical exporter does
not yet rebuild nested callable payloads, after confirming it imports to the
published `TypeId`. Qualified static-member semantic slots now carry the
interned descriptor directly, and parser-facing type queries reuse the resolved
declaration syntax when a structural export is unavailable. This covers
`sizeof` on a qualified static member with a nested callable alias, including
the pointee array reached by dereference or built-in subscript. Expression
`sizeof` now reads object size from the resulting canonical type instead of
requiring a lossy parser-facing type export. General conversion-descriptor
consumption of alias wrappers remains open; see
[known issues](KNOWN_ISSUES.md).
`UnsupportedStaticMemberType` (1020) remains a fail-closed guard for canonical
type families not yet imported.

Class-template substitution now carries dependent pointer-to-array member
bounds onto the instantiated pointee type while preserving pointer-sized
member layout. Regression coverage checks size, reads, and writes across
different element types and bounds in
`tests/test_template_ptr_to_array_member_subscript_ret0.cpp`.

Alias-template substitution now retains ordered pointer/array wrappers and
their bound records through nested forwarding aliases. Canonical type
descriptors also preserve an unknown outer bound alongside known inner extents,
so `T (*)[][N]` keeps pointer-sized layout, row `sizeof`, and row-major
subscript behavior after instantiation. The regressions are
`tests/test_alias_template_dependent_pointer_to_unknown_outer_array_bounds_ret0.cpp`
and `tests/test_alias_template_forwarded_pointer_to_array_bounds_ret0.cpp`.
The negative regression
`tests/test_alias_template_forwarded_pointer_to_array_invalid_bound_e1817.cpp`
checks that concrete nonpositive bounds still diagnose through nested alias
forwarding.

The explicit declarator frame stack keeps nested declarator depth off the native
call stack. The recorded Clang stack-usage probe measured `parse_declarator` at
5,160 bytes versus 5,000 bytes on `origin/main`; repeat the comparison when
changing recursive parser paths. The latest canonical architecture probe
reports `TypeSpecifierNode` at 600 bytes; the previous 520-byte handoff
measurement is stale.

The explicit-criteria rollup is **10/79 complete**. The boundary-3A criterion
that pointer-to-member overloads distinguish owner and pointee types is now
covered; passing tests or the breadth of landed code do not complete the
boundary. This slice advanced the importer/declarator coverage work item by
making callable-object call overload selection preserve each argument's value
category, so `decltype` of a call agrees with the runtime overload; it completes
no exit criterion on its own. The flat-field-absence criterion remains advanced
but incomplete for the type-trait consumer family, the lazy-constraint
evaluator, and trait-operand nominal and member-owner identity; the flat
classifier in `TypeTraitEvaluator.cpp` remains for the families listed under
remaining work item 2. Implementation effort is not yet estimated reliably.

## Next work

Continue boundary 3A in this order:

1. **Make `TypeId` the conversion currency.** Continue migrating parser-side
   overload ranking and remaining syntax-facing callers to the structural
   planner, including remaining conversions that require temporary
   materialization and unsupported callable pairs. Regular function-pointer
   pairs and same-owner member-function-pointer pairs, builtin arithmetic, array-decay,
   null-pointer, function-decay, object-pointer, and member-pointer reference
   temporaries plus direct derived-to-base reference and pointer conversions now
   use the canonical planner and base graph. Data-member-pointer pairs now
   compare owner and pointee `TypeId`s and support the public non-virtual
   base-to-derived owner conversion. Member-function-pointer base adjustments
   now use canonical owner schemas. Identical dependent `noexcept` expression
    identities compare structurally. Dependent ordinary function-pointer calls
    defer and rerank after substitution, as covered by
    `tests/test_canonical_dependent_noexcept_deferred_overload_ret0.cpp`.
    Explicit member-function-template addresses now substitute their arguments
    before forming a member-function-pointer type, and dependent calls retain the
    overload set when that argument type is unavailable until instantiation. The
    `noexcept(true)` / `noexcept(false)` case is covered by
    `tests/test_canonical_dependent_member_noexcept_deferred_overload_ret0.cpp`.
    Class-template specialization owners now retain canonical `TypeId` identity
    through member-pointer import, substitution, and export. Exact owner
    comparison is covered by the canonical type unit test, and
    `tests/test_canonical_dependent_member_noexcept_class_template_overload_ret0.cpp`
    checks overload selection after substituting `Callable<T>::run`'s exception
    specification. Member-pointer conversions between distinct class-template
    specialization owners now use a `TypeId`-keyed inheritance graph, including
    mixed paths through ordinary records; concrete `int` and `char` owner
    conversions are covered by
    `tests/test_canonical_class_template_member_pointer_base_conversion_ret0.cpp`.
    The dependent-source case is covered by
    `tests/test_canonical_dependent_class_template_member_pointer_base_conversion_ret0.cpp`:
    `Base<T>` member addresses select concrete `Derived<int>` and
    `Derived<char>` owner candidates after substitution. Dependent
    member-function-pointer aliases now preserve deferred `decltype` targets
    until substitution can resolve a nested owner alias such as
    `OwnerBox<T>::type`. The regression
    `tests/test_canonical_dependent_member_template_pointer_argument_substitution_ret0.cpp`
    checks overload selection through both that alias and a direct `decltype`
    cast for `noexcept(true)` and `noexcept(false)` specializations.
    `tests/test_canonical_dependent_member_template_pointer_deep_alias_overload_ret0.cpp`
    covers a 32-hop alias-template chain over that same nested-owner member-
    function-template address on both MSVC and Itanium (no platform guard).
    Nested function-pointer objects such as `int (**)(int)` now keep distinct
    flat `FunctionPointer` identity from `int (*)(int)`: nested forms store the
    full pointer-wrapper stack in `pointer_levels` (two levels for `(**)`),
    single forms keep empty levels so import adds one category wrap, and both
    the canonical and compatibility planners reject depth mismatches as
    authoritative no-matches. The regression
    `tests/test_canonical_nested_function_pointer_overload_ret0.cpp` checks
    overload selection for single versus nested parameters. Address-of data
    members now publish declaring-class member-object-pointer identity for
    parser-time ranking, covered by
    `tests/test_canonical_member_object_pointer_address_of_exact_vs_base_conversion_ret0.cpp`.
    Member-object-pointer owners spelled as class-template specializations
    (`Holder<int>::*`) now publish specialization `TypeId` identity directly
    from the instantiated class declaration (with spelling retained only as a
    lexical projection), so overload ranking distinguishes `Holder<int>::*`
    from `Holder<char>::*` without requiring an intermediate alias or a
    name-map round-trip as owner authority. The regression
    `tests/test_canonical_template_id_member_pointer_owner_overload_ret0.cpp`
    checks parameter and alias forms. Member-function-pointer owners spelled
    as template-ids (`int (Holder<int>::*)()`) now enter the structural
    declarator candidate path (which previously rejected template-id owners)
    and publish specialization `TypeId` identity through the shared owner
    helper; `tests/test_canonical_template_id_member_function_pointer_owner_overload_ret0.cpp`
    checks parameter and address-of selection.     Using-alias MFP and mop spellings
    project owner `TypeId`/`EntityId` through alias resolution so cast ranking
    distinguishes same flat `type_index` shapes; covered by
    `tests/test_canonical_member_pointer_alias_owner_typeid_overload_ret0.cpp`.
    Alias-template mop/MFP targets with class-template owners now parse through
    the structural / `Owner::*` alias-target paths and rematerialize
    specialization owners at alias instantiation so substituted casts keep
    distinct owner `TypeId`s; covered by
    `tests/test_canonical_alias_template_member_pointer_owner_overload_ret0.cpp`
    (including type-parameter owners). Owner identity is preserved through
    alias-target substitution and republished with TypeId authority.
    Typedef member-function-pointer declarators (`typedef int (Owner::*Name)()`
    and template-id owners) now reuse `parse_declarator` so owner `TypeId`
    identity is published for cast overload ranking; covered by
    `tests/test_canonical_typedef_member_function_pointer_owner_overload_ret0.cpp`.
    Class-scope typedef mop/MFP aliases keep that owner identity through nested
    name lookup (`Typedefs::Field` / `Typedefs::Run`) via struct-relative
    TypeInfo publication and alias-owner projection; covered by
    `tests/test_canonical_class_scope_typedef_member_pointer_owner_overload_ret0.cpp`.
    User-defined conversion operators returning data- or member-function
    pointers now rank their trailing standard conversions canonically, so an
    exact member-pointer result beats the legal Base-to-Derived member-pointer
    conversion. `tests/test_conversion_operator_member_object_pointer_tail_ranking_ret0.cpp`
    checks both pointer families and executes the selected data-member-pointer
    overload.
    Sema now annotates evaluated conversion-function tails to data-member
    pointers, and code generation applies the canonical public non-virtual
    base offset while preserving the null member-pointer representation. The
    regression `tests/test_conversion_operator_member_object_pointer_tail_materialization_ret0.cpp`
    checks a nonzero base offset, invocation through the converted pointer, and
    null preservation.
    Dependent function-template addresses whose dependency is inside a nested
    callable return type now retain the overload set until substitution, then
    rank the concrete function-pointer candidates; covered by
    `tests/test_canonical_dependent_nested_callable_overload_ret0.cpp`.
    Dependent member-function-pointer variables whose signature contains
    substituted return and parameter types now retain that signature through
    AST substitution, allowing overload ranking to use the concrete callable
    type. `tests/test_canonical_dependent_member_function_pointer_signature_overload_ret0.cpp`
    checks selection for both scalar and record specializations in the presence
    of a competing member overload. A dependent `&Owner<T>::member` argument
    now keeps its overload set through point-of-instantiation call resolution,
    where non-template member-function-pointer parameter candidates are ranked
    by canonical signature and conversion; covered by
    `tests/test_canonical_dependent_member_function_pointer_contextual_address_overload_ret0.cpp`.
    Reference targets now use the canonical reference-binding planner, so
    address prvalues materialize for `const MFP&` and `MFP&&`, while an
    inaccessible selected overload remains a semantic diagnostic; covered by
    `tests/test_canonical_dependent_member_function_pointer_reference_contextual_address_ret0.cpp`
    and `tests/test_canonical_dependent_member_function_pointer_reference_access_e1617.cpp`.
    A single dependent member-function-template candidate can now deduce its
    function parameters and direct return-only template parameters from the target
    member-function-pointer signature; covered by
    `tests/test_canonical_dependent_member_function_template_address_overload_ret0.cpp`,
    `tests/test_canonical_dependent_member_function_template_return_only_deduction_ret0.cpp`,
    and `tests/test_canonical_dependent_member_function_template_address_access_e1617.cpp`.
    Structurally imported, unconstrained type-only member-function-template
    address candidates are now partially ordered by canonical function TypeIds,
    and incomparable viable candidates diagnose ambiguity. The
    `test_canonical_member_function_template_address_deep_partial_ordering_ret0.cpp`
    regression exercises 16 nested type arguments below the parser's current
    depth-20 limit. A single trailing type-template parameter pack is now
    imported with its canonical element TypeId and explicit pack position,
    deduced over the target signature's remaining parameters, and included in
    partial ordering. The regression
    `tests/test_canonical_member_function_template_pack_address_e1617.cpp`
    verifies that a fixed-leading pack overload is selected before access
    checking, and the canonical unit test covers pack deduction and ordering.
    Direct integral non-type function-template parameters used as arguments to a
    class-template specialization now retain `TemplateDeclId` plus parameter
    index identity and can bind to literal target arguments. The regression
    `tests/test_canonical_member_function_template_nttp_address_e1617.cpp`
    checks that `Buffer<Value>` outranks a generic `Type` overload and that
    access checking runs after that selection. The regression
    `tests/test_canonical_member_function_template_nttp_address_ambiguous_e1701.cpp`
    checks that `Buffer<Value, 1>` and `Buffer<1, Value>` remain incomparable;
    the canonical unit test checks declaration-identity binding and ordering.
    For equivalent member-template function patterns, a satisfied associated
    constraint now ranks ahead of an unconstrained candidate. The negative
    regression `tests/test_canonical_member_function_template_constraint_address_e1617.cpp`
    checks that the constrained private overload is selected for `int`, while
    the unconstrained overload remains viable when the constraint fails for
    `char`. The concept-parameter regression
    `tests/test_canonical_member_function_template_concept_address_e1617.cpp`
    checks the same candidate selection through a constrained template
    parameter. Full C++20 subsumption between two constrained candidates and
    constraint evaluation for templates containing packs remain unsupported.
    Complex NTTP expressions, non-literal dependent arguments, and non-type
    packs remain unsupported, as do pack forms beyond the single trailing type
    pack. A C-style ellipsis combined with a template pack is also unsupported.
    Nested class-template return types materialize
    return-only member-template bindings from canonical TypeIds; the
    `test_canonical_member_function_template_nested_return_only_deduction_ret0.cpp`
    regression covers both scalar and record arguments. Class-template
    specializations in structured member-function-pointer signatures now
    receive canonical TypeIds from published TypeInfo before import, and
    ordinary calls resolve each qualified member-template address candidate
    against the target signature instead of retaining the first overload's
    return hint. The regression
    `tests/test_canonical_dependent_member_template_owner_identity_overload_ret0.cpp`
    checks dependent scalar and record selection plus direct record selection.
    Unconstrained, type-only free-function-template address candidates now
    deduce and partially order
    against the expected function-pointer `TypeId` during ordinary-call
    conversion; direct free functions participate in the same contextual set.
    `tests/test_canonical_free_function_template_address_deduction_ret0.cpp`
    checks explicit target formation, template partial ordering, direct
    overload selection, and precedence over an ellipsis fallback. Constrained
    templates, packs, and non-type template parameters remain unsupported in
    this path. Evaluated address arguments still need IR materialization of
    the selected function specialization (see `docs/KNOWN_ISSUES.md`).
    Dependent member-function-pointer forms that do not import structurally
    and unsupported callable conversions still need substitution-aware
    canonical ranking.
    Ordinary function-pointer `decltype(&function<T>)`
    aliases now retain and substitute explicit function-template arguments
    before ranking.
Shared evaluation, constant-expression evaluation, and code-generation trait
lowering all route through that one classification, so a trait no longer
answers differently depending on whether it is folded or lowered. The regression
`tests/test_canonical_structural_type_traits_ret0.cpp` checks the family over
native scalars, `wchar_t`, arrays of every bound shape, pointer-to-array and
function-pointer depths, references, records, both enum forms, data- and
function-member pointers, a class-template specialization and its member
pointer, and interleaved pointer/array/function declarators, in both the
constant-expression and the lowered path. Before this change the folded path
answered `__is_arithmetic`, `__is_compound`, `__is_fundamental`, `__is_integral`,
`__is_scalar`, `__is_enum`, `__is_member_object_pointer`, and `__is_function`
from the flat projection and disagreed with the lowered path for arrays, record
and enum operands, member pointers, and function pointers. The two remaining
flat consumers of the family were removed rather than reconciled: the private
constant-expression switch and the code-generation `__is_bounded_array` /
`__is_unbounded_array` cases now delegate to the shared evaluator. The
`canonical_structural_trait_fallback` counter records unary property traits
answered from compatibility fields, has a fixed corpus and baseline in
`tests/migration_counters/corpus_baseline.tsv`, and must reach zero at the 3A
exit; it currently measures the record and enum operands reached through a
`decltype` that has not published its `EntityId`, and the lazy-constraint path,
which does not use the shared evaluator at all. A bare function designator's
`decltype` now keeps its function type instead of importing as a
pointer-to-function, so `decltype(freeFn)` is `int(int)` while
`decltype(&freeFn)` stays `int (*)(int)`;
`tests/test_function_designator_decltype_identity_ret0.cpp` covers the
distinction and the `__is_function` / `__is_pointer` answers. The decltype path
now also applies [dcl.type.decltype]'s reference rule to a parenthesized
id-expression, so `decltype((x))` is `int&` while `decltype(x)` is `int`;
`tests/test_decltype_parenthesized_lvalue_ret0.cpp` covers a scalar, a record,
a function designator, and the prvalue forms. The rule keys off the last
comma-operator operand, the one whose type `decltype` takes, so
`decltype((x), (x))` is `int&` while `decltype((x), x)` is `int`;
`tests/test_decltype_comma_operator_parenthesized_lvalue_ret0.cpp` covers the
parenthesized, bare, prvalue, and three-operand shapes. A dereference and
member access on an lvalue are recognized as lvalues too, so
`decltype((*p))` and `decltype((s.m))` are references;
`tests/test_decltype_parenthesized_lvalue_forms_ret0.cpp` covers the
dereference, member access, member-access chain, and prvalue forms. Built-in
array and pointer subscripts now resolve from their ordered declarator shape,
so `decltype(a[i])` and `decltype(p[i])` produce lvalue references while
preserving nested array bounds and element cv-qualification;
`tests/test_decltype_subscript_expression_ret0.cpp` covers those forms and the
C++20 reversed pointer-subscript form. Class `operator[]` selection stays with
sema. Callable-object calls now preserve each argument's lvalue value category
while the parser selects the `operator()` overload, so the parser-facing return
type that `decltype` reads matches the runtime call: an lvalue argument selects
the `T&`-returning overload instead of the `T&&`-returning one, and an xvalue
argument keeps `T&&`. The fix reuses `apply_lvalue_reference_deduction` at the
identifier and general postfix callable-call sites, the same helper the
direct-call paths already use.
`tests/test_decltype_call_operator_value_category_ret0.cpp` checks the lvalue,
xvalue, parenthesized, and temporary callable forms with a partial-specialization
reference-kind discriminator and a runtime binding. A callable stored as an
object data member (`object.callable(args)`) still falls back to the synthetic
return type, and calls whose argument value category is not known at parse time
remain open.

Overload-ranking tie-breakers for reference parameter identity and pointer

   qualification now compare supported imports structurally; unsupported types
   still use their compatibility tie-breakers. Continue making projectable
   semantic descriptors use structural identity and replace remaining
   flat-field reads with a single compatibility materializer at each legacy
   boundary. Preserve full callable comparison, nested cv, array decay, and
   value-category behavior.
2. **Migrate remaining flat consumers.** The structural `[meta.unary.prop]`
   family is done in the shared type-trait evaluator and in the lazy-constraint
   evaluator, nominal identity is published at parser materialization, and the
   class, union, qualification, and published polymorphic/final/abstract traits
   for completed records, including class-template specializations, are answered
   canonically. Next in order:
   1. **The constructibility family.** The zero-argument `__is_constructible`,
      `__is_trivially_constructible`, and `__is_nothrow_constructible` answers
      are classified from TypeId-keyed record facts in the folded and lazy
      paths. The argument-bearing forms still read `StructTypeInfo` through
      constructor overload resolution. The zero-argument queries reject an
      abstract class, a deleted default constructor, and an inaccessible default
      constructor, which the approximate record check previously reported as
      constructible. The trivially-constructible answer uses
      `hasTrivialDefaultConstructor`, and the nothrow answer reads a
      user-provided default constructor's exception specification; the reduced
      regressions are `tests/test_is_constructible_default_constructor_ret0.cpp`
      and `tests/test_is_trivially_nothrow_constructible_default_ret0.cpp`.
      `__is_constructible` now walks the base and member subobject graph with an
      explicit worklist and stops at a user-provided default constructor, so a
      derived class whose base has no usable default constructor is no longer
      reported constructible; see
      `tests/test_is_constructible_subobject_recursion_ret0.cpp`. The
      `DefaultConstructible`, `TriviallyDefaultConstructible`, and
      `NothrowDefaultConstructible` facts are published by class `TypeId` in
      `CanonicalRecordProperties` (`CanonicalRecordConstructionFlags`), and the
      zero-argument queries for all three variants are answered from them in the
      folded and lazy paths;
      `tests/test_canonical_lazy_default_construction_concept_ret0.cpp` and
      `tests/test_canonical_lazy_trivial_nothrow_construction_concept_ret0.cpp`
      check the classified lazy answers; the default-construction query imports
      its operand through the structural-trait importer, so a class-template
      specialization projected as a nominal specifier recovers its published
      EntityId instead of falling back, which lowered that regression's
      `lazy_constraint_trait_fallback` to 0. The trivial and nothrow answers share
      the base/member walk, so a non-trivial member or base makes default
      construction non-trivial and a throwing member makes it throwing. An
      argument-bearing constructibility requirement now resolves through
      constructor overload resolution in the lazy path for a record target, and
      through the implicit conversion rules for a scalar, reference, or pointer
      target, matching the folded path. The shared
      `constructibleFromArgument` conversion check is used by both paths;
      `tests/test_canonical_variadic_constructibility_concept_ret0.cpp` and
      `tests/test_canonical_variadic_nonrecord_constructibility_concept_ret0.cpp`
      check a matching constructor, an arity mismatch, a record argument, a
      missing int constructor, and scalar and pointer conversion targets. A
      class-type default member initializer that default-constructs the member
      now contributes to the nothrow answer, so a member initialized as
      `Member member{}` makes the class nothrow only when `Member`'s default
      constructor is. Other default member initializer expressions need
      expression-level noexcept evaluation, and the canonical (non-sema) form
      of the argument-bearing query remains. Code generation now delegates the
      three constructibility kinds to the shared evaluator instead of its own
      approximate switch, which removed the duplicate logic and keeps the
      folded, constexpr, and lowered answers on one classification. The unary triviality and lifetime traits now use TypeId-keyed
      record-property facts. `CanonicalRecordLayout` publishes object size,
      member offsets, and the union flag. `CanonicalRecordProperties` publishes
      unary record-property facts for completed record TypeIds. Class-template
      instantiation refreshes virtual metadata and layout after attaching member
      declarations, propagates deleted special-member facts, and then publishes
      these facts. Remaining: the exception specification contributed by a
      default member initializer that is not a default construction (needs
      expression-level noexcept evaluation), and the canonical (non-sema) form
      of the argument-bearing query.

      Code generation delegates these rules and the assignability forms to the
      shared evaluator through `isRecordPropertyTraitOwnedBySharedEvaluator`.
      The shared implementation now walks record members and bases for
      standard-layout and destructor-triviality answers, composes POD from
      triviality and standard-layout, and detects inherited virtual destructors.
      Standard-layout checks include the C++20 zero-offset member-type rule
      through nested records, arrays, and unions.
      Constant evaluation delegates aggregate and virtual-destructor queries to
      that same evaluator. The 294-cell differential was rerun after these
      fixes; every trait/type cell now matches clang in constant evaluation and
      runtime lowering. The matrix regression keeps a separate assertion and
      runtime assignment for each cell across 21 record shapes, including a
      virtual-base class without virtual functions. Focused
      regressions also cover zero-offset base/member conflicts, member arrays,
      first-declaration versus out-of-line defaulted constructors and
      destructors, and deep derived-record cases. The triviality and
      trivially-copyable
      record walks use explicit worklists; a 511-level nested-record regression
      passes, including nothrow destructibility. Clang stack-usage output shows
      fixed native frames: 424 bytes for nothrow destructibility (the previous
      recursive predicate used 136 bytes per nesting level), 376 bytes for the
      shared worklist traversal, and 536 bytes for standard-layout. The full
      suite passes after these changes. The remaining implementation still
      reads `StructTypeInfo`. Moving it to canonical types requires publishing
      member, base, and special-member properties alongside the class facts.
      The separate uninitialized-`bool`-local symptom found during the earlier
      audit was a dropped `bool` cast rather than a storage problem and is
      diagnosed under "A cast whose source is `bool` is dropped" in
      [known issues](KNOWN_ISSUES.md).
   2. **Confirm a gap with a counter before adding identity plumbing.** A
      consumer that takes a `const TypeSpecifierNode&` cannot stamp its operand,
      and that signature alone is not a gap: the operand already carries its
      published `EntityId`. Do not add a non-mutating identity accessor, an
      entity-taking importer overload, or a counter on that reasoning. Put a
      counter at the import site first and run the fixed corpus; if it reads
      zero, there is nothing to close and the new API would be permanent dead
      weight. If it reads nonzero, capture a reduced regression that fails and
      land the accessor with it.
   3. **Template argument and substitution storage.** The lazy constraint
      evaluator still substitutes a template parameter by name against
      `template_param_names`; boundary 6 replaces that with depth-and-index
      parameters. Then constexpr type queries and IR layout/subscript paths. Add
      reduced non-library regressions for language rules. Make callable `TypeId`s
      authoritative through signature substitution so each `FunctionType` no
      longer carries a duplicate ordered spine beside its flat projections. Keep
      unsupported shapes fail-closed until their consumers are structural.
3. **Complete importer and declarator coverage.** Add canonical import support
   for remaining valid ordered forms still rejected at a boundary, including
   alias array, reference, and member-pointer wrappers. Keep member `TypeId`s
   preserved through every AST copy and substitution route as those paths
   migrate. An abstract function type is now parsed as a type-id argument and a
   template argument, so `__is_same(Fn, int(int))` and `Box<int(int)>` parse;
   `tests/test_function_type_type_id_argument_ret0.cpp` covers the spelling,
   its distinction from a function pointer, and the template-argument form.
   The `decltype` reference rule now covers a parenthesized id-expression,
   dereference, member access on an lvalue, and built-in subscripting, and keys
   off the last comma-operator operand. Callable-object calls now propagate an
   argument's lvalue value category into `operator()` overload selection, so
   `decltype` of a call through an identifier, a parenthesized callable, or a
   temporary matches the runtime overload's return type (including `T&` and
   `T&&`); `tests/test_decltype_call_operator_value_category_ret0.cpp` checks
   those forms. Remaining: a callable stored as an object data member
   (`object.callable(args)`) and a call or xvalue operand whose value category
   the parser does not know at parse time.
4. **Close and mutation-validate the 3A exit criteria.** Prove independence
   from parser/context stacks, parse order, and string insertion order; cover
   remaining pointer-to-member, function, dependent, and template families;
   and remove flat pointer-level and array-dimension fields from migrated
   semantic paths.

Before declaring 3A complete, check every exit criterion in the plan and read
its [parallel architecture experiment](2026-08-24-parallel-front-end-architecture-experiment.md).
Boundary 3B (ABI-conforming mangling) and boundary 4 (authoritative expression
sema) follow 3A. Do not route around unfinished canonical-type ownership to
start them.

## Other open boundaries

- **Scratch rollback and publication:** `FrontendScratchTransaction` journals
  scratch state, `DeclarationBuilder`, `TemplateDeclTable`, bound
  `SymbolTable` publication maps and in-place global/namespace variable array
  type normalization, plus namespace creation/declaration/inline metadata.
  Nested commits remain provisional until the outer transaction commits. Scope
  creation, cursor movement, and `ScopeRecord` publication are outside this
  boundary, and production speculative parsing is not yet integrated with the
  transaction. The namespace registry is process-global, so publication
  transactions spanning multiple live `FrontendContext`s are not isolated;
  see [known issues](KNOWN_ISSUES.md).
- **Compiler bugs and bounded unsupported cases:** consult
  [known issues](KNOWN_ISSUES.md) before selecting adjacent work. Add newly
  found bugs there; keep this file focused on migration work still ahead.
- **Callable ABI mangling:** MSVC name mangling still throws an internal error
  for function declarations with non-projectable ordered callable parameters;
  address this at boundary 3B after canonical type migration.
- **Boundary 4 handoff - member-function-pointer descriptor identity:** a
  concrete member-function-pointer target whose owner survives only as the
  function signature's class name reaches `tryImportCanonicalTypeDesc` with
  `CanonicalTypeDesc::structural_type_id` unset. `materializeTypeSpecifier`
  then rebuilds the node from the flat `type_index` plus that signature, so the
  canonical importer sees no owner `TypeId`/`EntityId` and returns
  `UnmigratedCallable`. Trace: `checkMemberFunctionAddressAccessForTarget`
  (`SemanticAnalysis.cpp`) -> `tryImportCanonicalTypeDesc` ->
  `materializeTypeSpecifier` -> `set_function_signature`. Extending the
  `canonicalizeType` member-pointer branch near the member-object-pointer block
  did not fire, so the descriptor is produced by a different semantic
  normalization path. First split: populate `structural_type_id` on
  member-function-pointer descriptors in the semantic type context from the
  canonical owner `TypeId` (not the `type_index` or the signature spelling),
  then let materialization reconstruct the owner. This blocks the remaining
  dependent member-function-pointer `UnmigratedCallable` shapes.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (for example, `_e1001.cpp` or `_e1003_e1051.cpp`). `_fail.cpp` is
  reserved for the immutable legacy inventory.

## Active findings

- Scratch `allocateObject` can construct an object before destructor-vector
  registration throws `bad_alloc`, leaving its destructor unregistered. Fix
  this before production scratch probes use nontrivial objects.
- The top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md).

## Validation handoff

After compiler-source changes, build with `.\build_flashcpp.bat`. Run focused
regressions while iterating, then `pwsh tests/run_all_tests.ps1` when the change
is ready. Never run the full suite concurrently with the build.

Migration counters and static identity inventories are baselined under
`tests/migration_counters/`; run the host-native counter and inventory scripts
after compiler changes. On 2026-10-05 all fixed-corpus entries remained within
baseline, including `canonical_structural_trait_fallback` at 0 on the
structural-trait and lazy-constraint regressions (baseline lowered from 23 so a
reappearance fails). The remaining four lazy-constraint fallbacks are the
constructibility probes, which need the canonical constructor-query path. The
inline dollar-recovery inventory and the canonical-adapter source corpus remain
within their supported/deferred baselines. Gate 0's Windows and ELF
multi-translation-unit checks remain required compatibility evidence. See the
plan for complete boundary-specific validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use remains bounded as logical depth grows. Do not raise the
stack limit to make a regression pass. The trait-classification slices add no
recursion: the structural classifier is an iterative node walk over the
canonical table with no new native frame, the property switch is a flat jump
table, and the lazy constraint evaluator's `&&`, `||`, and `!` handling
propagates the same result type it already had.

One known coverage gap: the compatibility `TypeTraitEval::isSigned` and
`isUnsigned` adapters are not exercised anywhere in the corpus. Replacing their
bodies with `return false` leaves all 3124 single-file tests green, because the
canonical classification answers every signedness question the corpus asks and
the fallback only runs when a canonical import is unavailable. They are kept as
the compatibility path for unmigrated operands, and a regression that forces an
unimportable operand through them is still owed.
