# Front-end migration progress

This is the handoff for agents continuing the migration. The [rearchitecture
plan](2026-08-24-front-end-rearchitecture-plan.md) is authoritative for the
design, boundaries, and exit criteria. This file records current state and
next work; completed implementation history belongs in git.

Last updated: 2026-09-28.

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
operands, and shared/constant-evaluation `__is_pointer` and `__is_array` now
classify the canonical outer wrapper for supported imports. Other traits,
lazy-constraint trait evaluation, and template, constexpr, and IR consumers
still read flat fields. Array and callable outer wrappers remain guarded where
their consumers are not migrated.

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
array decay, and pointer/array-to-`bool` conversions. Same-shape reference
binding, including exact-shape prvalues that materialize for `const` lvalue
references, now plans from canonical `TypeId`s
while carrying expression value category separately. Rvalue binding retains
exact-match rank when it adds top-level cv. Imports stay within builtin, record,
or enum base types. Speculative imports roll back; standard builtin-to-builtin
conversions can now bind eligible references through a temporary, such as an
`int` value converted to `double` for `const double&`; an lvalue converted to
`double` can also bind to `double&&`. Array lvalues now decay canonically when
binding pointer temporaries to eligible const lvalue and rvalue references.
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
overload selection for object and data-member pointers. Parser support for
function-pointer and member-function-pointer reference declarators remains
deferred; see [known issues](KNOWN_ISSUES.md).
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
Dependent `decltype(&Callable<T>::member)` aliases now recover the substituted
member-function signature and canonical owner before overload ranking.
`tests/test_canonical_distinct_dependent_member_noexcept_pair_overload_ret0.cpp`
checks that distinct dependent exception expressions select the non-throwing
target for `int` and reject it for `char` when the source member is potentially
throwing.
`tests/test_canonical_one_sided_dependent_member_noexcept_pair_overload_ret0.cpp`
checks a dependent source exception specification against fixed non-throwing
and potentially-throwing target aliases after class-template substitution.
Deferred ordinary function-template addresses now retain their explicit
template arguments through substitution, so `decltype(&function<T>)` aliases
use substituted signatures for overload ranking. Exact type identity for a
concrete alias-template specialization remains a separate gap in
`docs/KNOWN_ISSUES.md`.
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
member-owner, function-signature, and base-to-derived selection on MSVC.
Itanium end-to-end coverage is deferred until boundary 3B supports mangling
member-function-pointer parameter types. Dependent `noexcept`,
user-defined conversions, and other unsupported callable or template types
still use compatibility planning.
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
shapes. Object-pointer conversions that create a pointer temporary can now bind
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
conversion. `tests/test_canonical_array_decay_reference_overload_ret0.cpp`
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
boundary. Implementation effort is not yet estimated reliably.

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
    member-function-pointer forms beyond class-template `decltype` aliases and
    remaining unsupported callable pairs still need substitution-aware
    canonical ranking. Ordinary function-pointer `decltype(&function<T>)`
    aliases now retain and substitute explicit function-template arguments
    before ranking.
   Then make projectable semantic descriptors use structural identity
   too, and replace flat-field reads with a single compatibility materializer
   at each remaining legacy boundary. Preserve full callable comparison,
   nested cv, array decay, and value-category behavior.
2. **Migrate remaining flat consumers.** Extend canonical classification from
   `__is_pointer` and `__is_array` to the other type traits and lazy constraints,
   then prioritize template argument/substitution storage, constexpr type
   queries, and IR layout/subscript paths. Add reduced non-library regressions
   for language rules. Make callable `TypeId`s authoritative through signature
   substitution so each `FunctionType` no longer carries a duplicate ordered
   spine beside its flat projections. Keep unsupported shapes fail-closed until
   their consumers are structural.
3. **Complete importer and declarator coverage.** Add canonical import support
   for remaining valid ordered forms still rejected at a boundary, including
   alias array, reference, and member-pointer wrappers. Keep member `TypeId`s
   preserved through every AST copy and substitution route as those paths
   migrate.
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
after compiler changes. On 2026-09-28 all 64 fixed-corpus entries remained
within baseline, the inline dollar-recovery inventory measured 16 against a
baseline of 17, and the canonical-adapter source corpus remained within its
supported/deferred baseline. Gate 0's Windows and ELF multi-translation-unit
checks remain required compatibility evidence. See the plan for complete
boundary-specific validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use remains bounded as logical depth grows. Do not raise the
stack limit to make a regression pass.
