# Front-end migration progress

This is the handoff for agents continuing the migration. The
[rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md) is
authoritative for the design, boundaries, and exit criteria. This file records
the current boundary state, the remaining work, and the validation contract;
completed implementation history belongs in git.

Last updated: 2026-10-14.

## Current state

Boundary **3A (canonical types)** is active and incomplete. The explicit-criteria
rollup is **10/79 complete**; the only completed 3A exit criterion is that
pointer-to-member overloads distinguish owner and pointee types. Passing tests or
the breadth of landed code do not complete the boundary.

The architecture the remaining work builds on:

- `TypeSpecifierNode` carries an outermost-to-innermost `DeclaratorComponent`
  spine for named and abstract pointer/array/function/member-pointer declarators.
  The legacy flat pointer/array fields are projections retained only for
  unmigrated consumers.
- `CanonicalTypeTable` (owned by `FrontendContext`, single-mutex) interns and
  substitutes structural types keyed by `TypeId`, and publishes by `EntityId` or
  `TypeId`: record layout, record member/base/field schemas, class-base schemas,
  named type members, enum layout (underlying type and fixed/unfixed promotion),
  and record property facts. `TypeId` is the external identity; `StringHandle` is
  spelling only.
- `TypeContext` stores canonical descriptors in a pinned chunked arena, so a
  `CanonicalTypeDesc` reference returned by `get` stays valid while later
  descriptors are interned; `CanonicalTypeId` remains the identity.
- `canonicalizeType` publishes `structural_type_id` for a projectable pointer or
  array when the canonical importer can represent that shape. `TypeContext::intern`
  does the same for pointer and array descriptors built outside `canonicalizeType`,
  after copying off any `get()` reference, and re-derives the id from the
  descriptor's current flat projection so a stale id cannot survive a projection
  change. Conditional array decay, conditional
  pointer selection, and built-in subscript element extraction read that id and do
  not import while a descriptor reference is
  live. Flat pointer-level and array-dimension fields remain for shapes the
  importer cannot represent, and as the materialization projection of a published
  pointer or array (`PublishedPointerArray`). Exit criteria 4 and 5 are not
  closed: other semantic paths still read the flat projection. The 3A rollup
  remains 10/79.
- The shared type-trait evaluator classifies the structural `[meta.unary.prop]`
  family plus class/union/qualification and published record properties from
  canonical identity; the folded, constexpr, lazy-constraint, and lowered paths
  route through it. Zero-argument constructibility variants answer from published
  record facts.
- Parser-side overload ranking and semantic conversions use the structural
  planner and canonical base graph for builtin arithmetic, array decay,
  null-pointer, function decay, object-pointer, member-pointer reference
  temporaries, direct derived-to-base reference and pointer conversions, regular
  function-pointer and same-owner member-function-pointer pairs,
  conversion-function member-pointer-to-`bool` tails, public nominal
  derived-to-base tails (including template-specialization bases when their
  class-base schemas are available), conversion-operator results bound to
  const lvalue references and rvalue references, and the member-pointer owner graph.
- `decltype` implements the `[dcl.type.decltype]` value-category rule for
  parenthesized id-expressions, the last comma operand, dereference, member
  access, built-in subscripting, and callable-object calls.
- Class-template substitution, alias-template forwarding, and member-pointer
  owner identity preserve canonical `TypeId`s across AST copies and substitution.
  An ordered member-function-pointer declarator whose callable return carries its
  own declarator (for example `int* (Owner::*)() const`) imports structurally:
  the spine's return components fold into the canonical function signature and
  the owner `TypeId` is preserved. MSVC and Itanium mangling fold the same spine
  components into the callable's return type instead of rejecting the form, and
  IR lowers the value as a fixed-size scalar through the declaration, parameter,
  return, and identifier paths. A template-parameter owner
  (`int* (T::*)() const`) is published as that class's canonical identity during
  substitution, before a dependent `noexcept` operand is canonicalized.

Compatibility counters and identity inventories are baselined under
`tests/migration_counters/`; the flat classifier in `TypeTraitEvaluator.cpp`
remains only for the families listed under remaining work item 2.

Boundary **2F** is complete. The archived legacy negative reproducers under
`tests/unsupported_boundary_2f/` have all been recovered as encoded `_e<ID>`
negative tests and the archive index is empty. The final reproducer, an
out-of-line static member function of a class template that references a
non-static data member, is rejected with `NonStaticMemberUseInStaticFunction`:
a replayed out-of-line member body now surfaces its structured rejection instead
of being masked by a generic `OutOfLineMemberNotAttached` attachment failure.

## Next work

Continue boundary 3A in this order.

1. **Make `TypeId` the conversion currency.** Continue migrating parser-side
   overload ranking and remaining syntax-facing callers to the structural
   planner, including conversions that require temporary materialization and
   unsupported callable pairs. Continue making projectable semantic descriptors
   use structural identity and replace remaining flat-field reads with a single
   compatibility materializer at each legacy boundary, preserving full callable
   comparison, nested cv, array decay, and value-category behavior. Remaining:
   unsupported callable conversions that still need substitution-aware canonical
   ranking; derived-to-base conversions through non-projectable declarators and
   callable-component conversions, which remain deferred; conversion-function
   tails for unsupported callable relations. Nominal tails use published
   class-base schemas to rank unique base paths regardless of access, and an
   evaluated call now records the selected conversion operator (with its
   derived-to-base or member-pointer tail) so codegen materializes it.
   Post-selection access is applied to the selected conversion function and to
   its derived-to-base step.
   The ordered canonical planner handles supported non-nominal tails. Array
   and callable outer wrappers stay guarded where their consumers are not
   migrated.
   `FunctionDeclarationNode` now retains the `explicit` specifier for conversion
   functions, so an explicit conversion function is excluded from implicit
   viability (an argument or copy-initialization no longer selects it) while a
   `static_cast` or direct-initialization still considers it, and both forms now
   lower by calling the selected operator rather than reinterpreting the object.
   Conversion-operator selection uses the sema-owned canonical `TypeId` path
   first for `static_cast`, initialization, returns, ordinary call arguments,
   assignment, arithmetic promotions, conditional branches, contextual `bool`,
   constructor arguments, and builtin subscript conversions. It traverses
   canonical class-base identities while reading candidate declarations through
   a `StructTypeInfo` compatibility bridge. Candidate hiding now keys on the
   full return `TypeId`, preserving pointer and callable target shape, and
   selected canonical candidates carry their declaring class `TypeId` through
   access checking. The explicitly named
   `trySelectCompatibilityUserDefinedConversionOperator` remains available
   only when source import or the declaration bridge cannot resolve the class.
   Constructor-conversion probes now take source and target syntax and consult
   the published constructor schema by canonical `TypeId` for supported builtin
   and pointer conversions, including explicit-constructor filtering. Direct
   reference bindings now share a TypeId planner with the constructor trait
   query, including same-referent and public derived-to-base bindings, cv and
   value-category checks, and rejection of inaccessible or ambiguous bases.
   Class values, non-builtin reference conversion temporaries, dependent types,
   and unsupported callable argument shapes still use the compatibility
   resolver when the schema cannot prove the answer. Member
   pointer owner conversions now publish and query canonical class-base schemas
   before using the legacy TypeIndex owner bridge.
   Shared unary and binary operator ranking now traverses class and base
   identities with canonical `TypeId`s; candidate declarations still come from
   the `StructTypeInfo` compatibility bridge. Parser and IR entry points that
   only have a `TypeIndex` adapt at the boundary. TypeId-driven operator ranking
   now matches current-instantiation self references by canonical class identity
   and `TemplateDeclId`, then projects the owner through `TypeInfo` for the
   existing `TypeSpecifierNode` conversion descriptor. TypeIndex self-reference
   entry points now adapt through canonical owner identities when importable;
   TypeInfo-based matching remains only as a compatibility fallback.
   The selected function is recorded in `ImplicitCastInfo`; call arguments,
   returns, variable initialization, assignment, contextual `bool`, constructor
   arguments, and builtin subscripting now lower that declaration without
   codegen-side overload lookup. `findConversionOperator` and its recursive
   TypeIndex search are removed. `static_cast` and direct-initialization both
   select through the TypeId path and fail closed in codegen when the annotation
   is absent. IR operands still use TypeIndex projections where existing backend
   call and storage operations require them. The compatibility path is still
   needed for source shapes the canonical importer or declaration bridge cannot
   yet represent:
   `tryAnnotateConversionByTypeId` now backs all shared implicit-conversion
   annotations. Its generic residual tail selects and records the conversion
   function, including the cv-ranked choice for a reference-qualified source.
   Its residual tail also handles class aliases by value, and its
   reference-stripped path records the selected operator for class aliases.
   Ordered declarator compatibility ranking now compares base identities with
   canonical `TypeId`s whenever both bases import, retaining `TypeIndex` only
   when the canonical importer cannot identify a base. The TypeId selector
   still reads candidate declarations from `StructTypeInfo`; remaining work is
   to remove compatibility selection where canonical import and declaration
   publication can cover the source, and finish unsupported callable and
   user-defined constructor ranking. Class-target
   variable initialization now consumes exact prvalue conversion selections
   for copy-initialization and parenthesized direct-initialization, constructing
   into the declared object.

2. **Migrate remaining flat consumers.**
   1. **The constructibility family.** The zero-argument variants and published
      record facts are canonical; the argument-bearing record query still reads
      `StructTypeInfo` through constructor overload resolution. Default member
      initializers now contribute their selected constructor's exception
      specification (scalar, single- and multi-dimension array, implicit or
      defaulted copy/move, constant `noexcept(expr)`, a class-template
      constructor whose `noexcept` operand is dependent and is re-evaluated per
      specialization, and an array element that default-constructs the member
      type from a prvalue `T{}` or an empty brace `{}` under guaranteed copy
      elision). Every function-like parse path now folds a constant `noexcept`
      operand and rejects a non-constant one with `NoexceptSpecifierNotConstant`
      (#1021) through one `resolveParsedNoexcept` / `applyParsedNoexcept` choke
      point, including static member functions, whose specifier was previously
      dropped, and destructors: a non-constant destructor operand is rejected
      with #1021, an explicit specifier is marked on the node, and a
      class-template destructor's retained dependent operand is re-evaluated at
      instantiation. Class-template member-function instantiation re-evaluates a
      retained dependent operand, and both `noexcept` evaluators now share the
      `tryResolveInstantiatedMemberNoexcept` member-call resolution through the
      receiver's type (`CallNodeHelpers.h`), so the member-function
      keyword-present interim answer is gone, and codegen `noexcept` lowering now
      delegates to the single `ConstExpr::Evaluator::is_expression_noexcept`
      authority; its own `AstToIr::isExpressionNoexcept` evaluator is deleted.
      `noexcept(expr)` is a compile-time constant, so the terminal fix is to fold
      it once in expression sema (boundary 4) and have lowering read the value.
      Remaining: an overloaded binary operator and a user-defined conversion
      function now consult their selected declarations, but a qualified
      static-member call is not yet resolved: `parse_primary_expression`'s
      qualified path (`lookup_qualified` returns null for `S::f`) builds a
      free-function callee, and resolving the static member there was reverted
      because sema then throws "Resolved member function access has no semantic
      owner class" — the same owner-class identity gap as the boundary-4
      member-function-pointer handoff (an earlier `parse_postfix_expression`
      resolver retry was likewise reverted), and the pseudo-destructor
      `noexcept` consumer resolves the
      instantiated destructor for cast, identifier, arrow, and dereference object
      forms but still falls back to the pattern for an object expression it cannot
      type; a class constructor call now consults the record's default
      constructor (zero-argument) or the unique user-provided same-arity
      constructor (argument-bearing), while an ambiguous selection stays
      conservative; the canonical argument-bearing query uses the class's
      published constructor schema to answer exact-match viability and selected
      user-provided constructor exception facts. The schema is keyed by canonical
      `TypeId` and published from the declaration node for both records and
      class-template specializations (no `StructTypeInfo` at publication).
      Fixed-arity candidates with supported canonical standard conversions use
      per-argument conversion ranks. These include builtin conversions,
      supported pointer conversions, same-owner member-pointer conversions, and
      array/function decay. Single-argument builtin-to-builtin scalar
      construction also uses the canonical planner; supported pointer and
      member-pointer scalar conversions use it as well, and `nullptr_t` to
      `bool` is handled as direct initialization. Single-argument reference
      targets use the canonical reference-binding planner for supported
      bindings and temporary materialization. Record constructor reference
      parameters with the same canonical referent or a public canonical base
      use direct reference ranking, while differing builtin scalar referents
      use promotion or conversion ranking for temporary binding to const
      lvalue references and rvalue references. Identical-referent direct-binding
      decisions remain authoritative, including rejection of mutable lvalue
      references to conversion temporaries; nullptr-to-bool reference arguments
      remain rejected. The shared TypeId-only planner replaces the local
      same-referent helper; canonical-only native tests exercise its ranks and
      the constructor probe and trait query without TypeInfo identities, and
      `test_canonical_constructor_base_reference_args_ret0.cpp` covers record
      and specialization bindings, rejection cases, overload ranking, and a
      lazy concept. `test_canonical_scalar_reference_temporary_args_ret0.cpp`
      covers builtin scalar lvalue/rvalue reference arguments, promotion versus
      conversion ranking, selected constructor exception facts, and a lazy
      concept. Enum and dependent types, class user-defined conversions, and
      pointer/callable referent conversions still defer to compatibility
      resolution. Direct constructor code generation also still needs to
      materialize scalar conversion temporaries before passing reference
      parameters; see [known issues](KNOWN_ISSUES.md). Unscoped enum
      arguments use published promotion and underlying types for builtin
      scalar and constructor parameters; scoped enum-to-builtin conversions
      are rejected canonically. Implicit copy/move constructor schemas derive
      their self-reference parameter TypeIds from the owning canonical
      identity when synthesized parameter nodes lack entity bindings.
      Variadic candidates and other record
      conversion shapes use constructor overload resolution to read the
      selected user-provided constructor's exception specification. The schema
      records trailing default-argument arity, so canonical viability accepts
      those calls; nothrow queries with omitted arguments defer because the
      default expression's exception behavior is not published. The triviality
      variant reads the selected constructor (user-provided is non-trivial; an
      implicit/defaulted copy or move is trivial exactly when the class is
      trivially copyable), and an
      implicit/defaulted nothrow selection follows the copy or move construction
      through the subobjects; a call's receiver/argument evaluation,
      which is not counted because `is_expression_noexcept` is boolean and cannot
      separate a known-throwing operand from one it cannot resolve (a blanket
      argument check was tried and regressed template noexcept cases, so it needs
      a tri-state result). Triviality and lifetime traits now walk published
      TypeId member/base schemas using direct special-member facts; unsupported
      or incomplete schemas defer to the compatibility classifier.
   2. **Recover nominal identity only when the counter proves the flat projection lost it.**
      `gCanonicalTypeEntityImportRecoveryCount` found 11 recoveries in the fixed
      corpus, including two in the reduced
      `test_canonical_trait_operand_entity_ret0.cpp` regression. The normalized
      trait operand now preserves its source `EntityId` and injected declaration
      before canonical import; the fixed corpus reports zero recoveries after
      that change. No new identity accessor was needed.
   3. **Template argument and substitution storage.** The lazy constraint
      evaluator substitutes a template parameter by name against
      `template_param_names`; boundary 6 replaces that with depth-and-index
      parameters, then constexpr type queries and IR layout/subscript paths.
      Callable signature substitution now stamps published type-parameter
      declaration/index identities through nested signatures with an explicit
      worklist. When both the pattern and argument import canonically, the
      substituted `FunctionType` retains the complete canonical `TypeId` and
      re-exports supported pointer/array/reference declarators instead of storing
      a duplicate ordered spine. Function identity comparison permits canonical
      components to match legacy projections, compares TypeIds when both are
      present, and keeps hashes representation-neutral.
      Explicit source NTTP expression identity now survives argument
      reclassification and the `TemplateTypeArg`/`TemplateArgInfo` round trip,
      so declaration-based class specialization import emits the same opaque
      NonType Spec link as syntax-based import when that identity is present.
      Dependent defaults now stay dependent until substitution, and simple
      member-function-pointer defaults carry the declared class/signature into
      specialization lookup. Regressions cover dependent integral defaults,
      defaulted member-function pointers, and explicit specialization stamps.
      Dependent record arguments and dependent qualified-id substitution retain
      an existing source ExprId when rebuilding a concrete NTTP. Stored and
      dependent-record value-pack expansion now evaluate each concrete element
      separately, preserving its typed value identity and source stamp, or
      interning an expanded expression stamp when no source stamp exists. Focused
      unit coverage protects typed identity and expression-stamp reconstruction.
      Deferred dependent-base arguments now preserve the evaluated
      `NonTypeValueIdentity` and source `ExprId` when materialized. Ternary
      arguments retain function-address identity by evaluating the selected
      branch through the NTTP-aware path; nested ternaries are traversed
      iteratively. `test_deferred_base_nttp_function_pointer_identity_ret0.cpp`
      covers a dependent-base argument selecting an explicit function-pointer
      specialization through a conditional expression. Other NTTP reconstruction
      paths remain outside this slice. Dependent stored NTTP arguments now also
      keep their evaluated value identity and original `ExprId` during
      materialization, interning the substituted expression when no source stamp
      exists. `test_materialize_stored_nttp_function_pointer_identity_ret0.cpp`
      covers function-pointer specialization lookup through this path.
      Member-alias materialization now preserves typed identity when resolving
      a dependent static-member NTTP, and early initializer normalization keeps
      pointer-to-member expressions available for that resolution.
      `test_member_alias_static_memberptr_nttp_identity_ret0.cpp` protects the
      specialization lookup. This extends the NTTP follow-up without completing
      a 3A exit criterion; the 3A rollup remains 10/79.
      Non-projectable function/member-pointer shapes remain deferred until
      canonical substitution and export coverage is available.

3. **Complete importer and declarator coverage.** Add canonical import support
   for remaining valid ordered forms still rejected at a boundary, including
   alias array, reference, and member-pointer wrappers, and keep member `TypeId`s
   preserved through every AST copy and substitution route. Remaining: a fully
   non-projectable ordered-spine `decltype` dereference (deferred to sema) and a
   call or xvalue operand whose value category the parser does not know at parse
   time. `UnsupportedStaticMemberType` (1020) remains a fail-closed guard for
   canonical type families not yet imported.

4. **Close and mutation-validate the 3A exit criteria.** Prove independence from
   parser/context stacks, parse order, and string-table insertion order; cover
   the remaining pointer-to-member, function, dependent, and template families;
   and remove the flat pointer-level and array-dimension fields from migrated
   semantic paths.

Before declaring 3A complete, check every exit criterion in the plan and read its
[parallel architecture experiment](2026-08-24-parallel-front-end-architecture-experiment.md).
Boundary 3B (ABI-conforming mangling) and boundary 4 (authoritative expression
sema) follow 3A. Do not route around unfinished canonical-type ownership to start
them.

### Bounded unsupported cases

These are unsupported or deferred today and seed adjacent slices; the
authoritative inventory is [known issues](KNOWN_ISSUES.md).

- Constraints: complex non-type concept argument expressions, constraints over
  packs or arrays, member/function dependent decorations, and forms that cannot be
  normalized leave candidates unordered; constrained free-function templates,
  packs, and non-type template parameters are unsupported in the
  address-deduction path; complex NTTP expressions, non-literal dependent
  arguments, non-type packs, and a C-style ellipsis combined with a pack are
  unsupported.
- Evaluated function-template address arguments still need IR materialization of
  the selected specialization.
- Virtual member-function addresses and multiple-inheritance member-pointer
  adjustment are unsupported, and calling through a materialized member-function
  pointer is unimplemented.
- General conversion-descriptor consumption of alias wrappers (callable, array,
  reference, member-pointer) remains open.
- The compatibility `TypeTraitEval::isSigned`/`isUnsigned` adapters are
  unexercised; a regression forcing an unimportable operand through them is owed.
- The canonical record constructor schema is published for a completed record
  independently of the field schema. A constructor whose parameter types do not
  import structurally is skipped, and the schema stays unpublished (consumers
  fall back to `StructTypeInfo`) only when no constructor is importable. The
  field schema is still unpublished for anonymous-union and unimportable-member
  records and is not published for class-template specializations, which do
  publish the constructor schema. These are canonical-importer gaps, not language
  rules, and seed the constructor/member-function schema migration.
- An explicit (full) class-template specialization now shares the primary's
  canonical identity. Its declaration records the primary's `TemplateDeclId` in a
  dedicated `canonical_specialization_owner` field (kept out of `template_decl_id`
  so member lookup and classification are unaffected) and its concrete arguments
  in `outer_template_args`; `importCanonicalClassSource` then builds the same
  `TemplateSpecialization` an implicit instantiation would, and the record and
  constructor schema publish under that TypeId. This stays on the canonical path
  (no `UnmigratedNominal` fallback and no legacy `DeclarationBuilder` publish,
  pinned by `tests/test_canonical_constructible_explicit_specialization_ret0.cpp`).

## Other open boundaries

- **Scratch rollback and publication:** `FrontendScratchTransaction` journals
  scratch state, `DeclarationBuilder`, `TemplateDeclTable`, bound `SymbolTable`
  publication maps, in-place global/namespace variable array type normalization,
  and namespace metadata. Nested commits stay provisional until the outer
  transaction commits; scope creation, cursor movement, and `ScopeRecord`
  publication are outside the boundary, production speculative parsing is not yet
  integrated, and the process-global namespace registry keeps transactions across
  multiple live `FrontendContext`s from being isolated. See
  [known issues](KNOWN_ISSUES.md).
- **Compiler bugs and bounded unsupported cases:** consult
  [known issues](KNOWN_ISSUES.md) before selecting adjacent work; add newly found
  bugs there.
- **Template-instantiation stack safety:** class-base preflight schedules plain
  nested-class member-type chains ending in a class-template-specialization alias
  (`tests/test_deep_nested_member_type_base_chain_ret0.cpp`); member-template
  segments and indirect alias targets remain open, and boundary 8A's AST-based
  instantiation and old-path deletion criteria are unfinished.
- **Callable ABI mangling:** MSVC name mangling still throws for function
  declarations with non-projectable ordered callable parameters; address at
  boundary 3B.
- **Boundary 4 handoff - member-function-pointer descriptor identity:** a
  concrete member-function-pointer descriptor now reconstructs without the
  compatibility projection. `canonicalizeType` stamps the published class
  identity onto the signature's structured return and parameter components
  (`bindPublishedClassComponents`), and `tryImportCanonicalTypeDesc` binds the
  owner spelling carried by the signature before importing, so
  `materializeTypeSpecifier` round-trips the member pointer and the importer
  returns `Supported`. Remaining: dependent-address NTTP cases still return
  `Unresolved`, and the identity stamp should move into the remaining signature
  writers so the centralized step can be dropped. Trace:
  `checkMemberFunctionAddressAccessForTarget` (`SemanticAnalysis.cpp`) ->
  `tryImportCanonicalTypeDesc` -> `materializeTypeSpecifier` ->
  `set_function_signature`.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (`_e1001.cpp`, `_e1003_e1051.cpp`); `_fail.cpp` is reserved for the
  immutable legacy inventory.

## Active findings

- The top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md).

## Validation handoff

After compiler-source changes, build with `.\build_flashcpp.bat`; run focused
regressions while iterating, then `pwsh tests/run_all_tests.ps1` when ready.
Never run the full suite concurrently with the build.

Run the host-native migration-counter and identity-inventory scripts under
`tests/migration_counters/` after compiler changes and keep every fixed-corpus
entry within baseline. `canonical_structural_trait_fallback` is 0 on the
structural-trait and lazy-constraint regressions (baseline lowered from 23 so a
reappearance fails); the residual lazy-constraint fallbacks are the
constructibility probes, which need the canonical constructor-query path. The
inline dollar-recovery inventory and the canonical-adapter source corpus remain
within their supported/deferred baselines. Gate 0's Windows and ELF
multi-translation-unit checks remain required compatibility evidence. See the
plan for complete boundary-specific validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use stays bounded as logical depth grows; do not raise the stack
limit to make a regression pass.

Constructibility has one shared authority: `evaluateConstructibility` in
`TypeTraitEvaluator.cpp` answers the zero-argument and argument-bearing questions
and takes a `ConstructibilityFallback` (the lazy path asks `None` and keeps its
unknown result; the folded and codegen paths ask `Sema`); the canonical helpers
are file-local, and default-construction facts and unary structural properties
share one `CanonicalRecordFacts` mask
(`tests/test_constructibility_paths_equivalence_ret0.cpp` pins path agreement).
Canonical imports use `tryImportSupportedCanonical(table, syntax)`, returning a
`TypeId` only on `Supported` and an empty result otherwise (including `Invalid`,
which callers recover from); it takes the table explicitly because resolving it
through `requireFrontendContext()` pulls `FrontendContext.h` into a
widely-included header and breaks the canonical-type mutation harness.

The recorded Clang stack-usage probe measured `parse_declarator` at 5,160 bytes
versus 5,000 bytes on `origin/main`; repeat the comparison when changing
recursive parser paths. The latest canonical architecture probe reports
`TypeSpecifierNode` at 600 bytes.
