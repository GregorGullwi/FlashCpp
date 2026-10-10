# Front-end migration progress

This is the handoff for agents continuing the migration. The
[rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md) is
authoritative for the design, boundaries, and exit criteria. This file records
the current boundary state, the remaining work, and the validation contract;
completed implementation history belongs in git.

Last updated: 2026-10-10.

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
   Conversion-operator selection is migrating to a single sema authority, with
   the end state that codegen never re-selects. The sema selector
   `trySelectCanonicalUserDefinedConversionOperator` has a canonical `TypeId`
   entry point for `static_cast`, variable initialization, returns, and ordinary
   call arguments; it traverses canonical class-base identities while reading
   candidate declarations through a `StructTypeInfo` compatibility bridge.
   Variable-initialization and return retain a `TypeIndex` fallback when a
   source specialization cannot be imported or resolved through that bridge.
   Shared overload candidate ranking and assignment, arithmetic, conditional,
   contextual-`bool`, and constructor-argument conversion callers still enter
   through `TypeIndex`.
   The selected function is recorded in `ImplicitCastInfo` and consumed by
   `emitSemaSelectedConversionOperatorCall`. The legacy codegen lookup
   `findConversionOperator` / `emitConversionOperatorCall` still uses compat
   `TypeIndex` currency and its own cv-aware overload choice. `static_cast` and
   direct-initialization lower through selected annotations and fail closed in
   codegen when one is absent. Direct-initialization still selects through
   `TypeIndex` with canonical target and tail planning. The legacy path is still
   load-bearing, not dead:
   `tryAnnotateConversion`'s generic residual tail now selects and records the
   conversion function, including the cv-ranked choice for a reference-qualified
   source. Its residual tail now also handles class aliases by value, and its
   reference-stripped path records the selected operator for class aliases.
   Constructor-argument codegen now requires the sema-selected conversion
   function and fails closed if the annotation is missing; it no longer performs
   a second cv-based lookup in that path.
   Other sema paths can still leave variable-initialization,
   return, call-argument, and contextual-`bool` codegen paths needing
   re-selection. The `TypeId` entry point still reads candidate declarations
   from `StructTypeInfo`, and most shared implicit-conversion callers still use
   the `TypeIndex` entry point. Long-term plan:
   (1) make every sema `UserDefined` annotation record the selected function,
   extending this to the remaining unselected paths; (2) migrate sibling codegen
   contexts to consume the annotation
   through `emitSemaSelectedConversionOperatorCall` and fail closed; (3) delete
   `findConversionOperator` / `emitConversionOperatorCall`, leaving one selection
   authority on the canonical `TypeId` currency. Until step 3 the two lookups can
   diverge on cv-aware overload choice, explicit filtering, and `TypeId` versus
   `TypeIndex` identity. The migrated `static_cast` and direct-initialization
   paths now fail closed when the annotation is absent, with the same
   `"sema must annotate"` internal error as the sibling paths. Class-target
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
      parameters with the same canonical referent use canonical ranking for
      qualification-only bindings; differing referents, dependent or nominal
      conversion tails, callable mismatches, and user-defined
      conversion shapes defer to compatibility resolution. Unscoped enum
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
      Non-projectable function/member-pointer shapes remain deferred until their
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
  concrete member-function-pointer target whose owner survives only as the
  signature's class name reaches `tryImportCanonicalTypeDesc` with
  `structural_type_id` unset, so `materializeTypeSpecifier` rebuilds it from the
  flat `type_index` and the importer returns `UnmigratedCallable`. First split:
  populate `structural_type_id` on member-function-pointer descriptors in the
  semantic type context from the canonical owner `TypeId`, then let
  materialization reconstruct the owner. Trace:
  `checkMemberFunctionAddressAccessForTarget` (`SemanticAnalysis.cpp`) ->
  `tryImportCanonicalTypeDesc` -> `materializeTypeSpecifier` ->
  `set_function_signature`.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (`_e1001.cpp`, `_e1003_e1051.cpp`); `_fail.cpp` is reserved for the
  immutable legacy inventory.

## Active findings

- Scratch `allocateObject` constructs an object before destructor-vector
  registration can throw `bad_alloc`, leaving its destructor unregistered. Fix
  before production scratch probes use nontrivial objects.
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
