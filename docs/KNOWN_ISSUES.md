# Known Issues

## Pointer-to-array declarator coverage gaps

`sizeof`/`alignof` type-ids with pointer-to-array declarators (`int(*)[3]`,
`const int(*)[3]`, `int(*const)[3]`) parse through the shared
abstract-declarator machinery; named multi-bound declarators
(`long (*table)[2][4]`) parse in every context (global, local, member,
parameter, function return); indexing through a pointer-to-array object or a
pointer-to-array struct member (`(*t)[i][j]`, `(*g.member)[i][j]`) lowers as
flattened row-major element access; and member/static-member registration,
canonical typing, constexpr sizing, and global bindings all preserve the
pointee shape via the `pointee_array_declarator` flag on `StructMember`,
`StaticMemberDecl`, `StructStaticMember`, and `CanonicalTypeDesc`.
Remaining gaps in the same area:

- Cast expressions cannot parse parenthesized abstract-declarator type-ids:
  `static_cast<int(*)[3]>(v)`, `reinterpret_cast<long(*)[2][4]>(v)`, and the
  C-style `(int(*)[3])v` all fail ("Expected '>' after type in static_cast" /
  "Expected primary expression"). The cast paths
  (`parse_cast_type_specifier` in `Parser_Expr_PrimaryUnary.cpp`,
  `consume_cast_type_id_postfix_modifiers`) consume only trailing cv- and
  ptr/ref-operators, not the parenthesized groups or `[N]` suffixes that
  `consume_type_id_abstract_declarators` (used by `sizeof`/`alignof`) accepts.
  Named spellings are unaffected: `int (*q)[3] = p;` parses and lowers
  correctly.
- Indexing through a class-template-instantiated pointer-to-array member
  (`Box<int> b; b.cells = &arr; (*b.cells)[i][j];`) crashes at runtime. The
  non-template struct path is fully working
  (tests/test_ptr_to_array_member_subscript_ret0.cpp); the instantiated-
  member case loses the pointee bounds somewhere in template substitution /
  lazy member resolution and falls back to non-flattened subscripting with a
  bad base.
- Out-of-line definitions of qualified static members spelled with
  parenthesized declarators (`long (*Registry::table)[2][4] = nullptr;`) are
  rejected by the parser ("Expected identifier token"). In-class
  `static constexpr` members work.

## Local aggregate initialization of multidimensional struct arrays

A local definition such as `struct P { int x, y; }; P pts[2][2] = {{{1,2},
{3,4}}, {{5,6},{7,8}}};` does not initialize the elements correctly: direct
element reads (`pts[0][0].x != 1`) fail before any pointers are involved.
Element-wise assignment after a bare declaration works, and file-scope arrays
with the same initializer work (tests/test_ptr_to_array_multibound_declarator_ret0.cpp
uses file scope for this reason). The bug is in local aggregate/initializer-
list lowering for nested braces over struct elements, not in the declarator
or subscript machinery.

## Member stores through a global struct pointer

With file-scope `W wv; W* wp;`, executing `wp = &wv; wp->tag = 7;` leaves
`wv.tag` unchanged (verified identical on the pre-pointer-to-array baseline).
Scalar/int globals, `long*` element writes, and `int**` chains through the
same binding-width fix behave correctly
(tests/test_global_pointer_binding_width_ret0.cpp). The arrow-store path for
aggregate members via a global binding needs its own investigation.

## Flat type representation cannot express interleaved pointer/array declarators

C++20 declarators compose recursively: in `int (*(*p)[3])[4]`, `p` is pointer
to array[3] of pointer to array[4] of int — array bounds interleave between
pointer levels. `TypeSpecifierNode` and `CanonicalTypeDesc` flatten a type into
a base category plus parallel `pointer_levels` / `array_dimensions` lists, so
only the two boundary shapes are representable: all dimensions outside the
pointers (`T* p[N]`) and all dimensions behind every pointer
(`T (*p)[N]`, `T (**pp)[N]`, `T (*p)[N][M]`), selected by the
`pointee_array_declarator` flag. Mixed interleavings such as
`int* (*p)[3]` (pointer to array of pointers) cannot be encoded. The paren
declarator path rejects a leading declarator star before the group, but an
alias carrying indirection would silently reorder the levels; canonicalization
therefore throws for that combination (`canonicalizeType`). Full conformance
requires replacing the parallel fields with an ordered declarator-component
sequence (or recursive type nodes) across AST, canonical types, template
substitution, mangling, traits, and codegen stride derivation.

## Variable-template initializer replay removed; static-member replay clones remain

RESOLVED for variable templates (2026-08-22, branch `opencode/alias-capture-identity`):
variable-template initializers are now substituted once, structurally, from the
declaration-time AST. Three mechanisms made that possible:

1. **Capture fix** — `Parser::rewriteDependentMemberTypeSpellings`
   (`Parser_Templates_Inst_Substitution.cpp`, hooked where the alias branch of
   `parse_type_specifier` returns an unresolvable dependent target,
   `Parser_TypeSpecifiers.cpp`) clones a dependent-member placeholder whose
   record still spells the alias body's own parameters (`remove_cv<T>::type`)
   into one spelling the use-site arguments (`remove_cv<Ty>::type`). Without it
   the stored initializer was unresolvable by any environment-based pass;
   replay used to mask this via leaked `template_param_substitutions_`.
2. **Replay deletion** — `try_reparse_variable_template_initializer` /
   `try_replay_variable_template_initializer` and their call sites in
   `try_instantiate_variable_template` are gone; both the primary and partial-
   specialization branches substitute the stored AST directly.
3. **Phase 3 correctness** — the recovery block now (a) prefers the pack-expanded
   use-site arguments over placeholder-stored arguments when they already name a
   registered specialization, and (b) rewrites the qualifier even when
   `try_instantiate_class_template` hits its already-instantiated cache (which
   returns nullopt). Previously a cache hit left the stale placeholder namespace
   in the initializer ("Undefined qualified identifier" at codegen).

Measured effect on Parsing phase: `tests/std/test_std_map.cpp` 8597 ms -> 6400 ms
(~26% faster); unit tests unchanged.

REMAINING WORK: four sibling lexer-replay sites still substitute static-member
initializers by re-parsing source text (in-class primary + partial-spec and
out-of-line clones in `Parser_Templates_Inst_ClassTemplate.cpp`, lazy clone in
`Parser_Templates_Lazy.cpp`). Migrating them onto structural substitution
requires the same capture guarantee for class-scope alias bodies plus the
member-context replay metadata those paths rely on. `Parser::ReplayTemplateBindings`
(pack-aware) keeps pack arity correct inside those replays meanwhile.

A structural-first flip of the in-class primary clone was attempted and
reverted (2026-08-22); it regressed two tests and exposed the exact ambient-
state coupling that replay currently absorbs:

- `test_template_default_qualified_arg_order_ret42.cpp` returned 44 instead of
  42. Trace chain: holder's NTTP default `pair_value<Z, A>::value` is evaluated
  by `substituteNonTypeDefaultExpressionImpl` with the correct frame
  (`params=Z,A,V args=4,2`) and correctly rewrites to
  `pair_value$eed27b09::value`; immediately afterwards a second substitution
  pass resolves the same stored owner arguments with `A -> 4` instead of 2,
  instantiates a bogus `pair_value<4,4>` whose member early-normalizes to 44,
  and that value becomes V. The wrong binding appears only when replay does
  not run first: some later re-resolution of the struct's stored instantiation
  context (early-normalizer / constexpr member lookup path) reads bindings
  polluted by an enclosing frame. Root cause to fix: make the instantiation
  context stored on completed class specializations authoritative (concrete
  values, not dependent spellings) and stop later passes from re-deriving
  member initializers through ambient `template_param_substitutions_`.
- `test_template_dependent_base_member_template_static_value_ret0.cpp` failed
  to link (`Derived$hash::value` unresolved): the structural result for a
  static member reached through a deferred base did not register the emitted
  global under the name main references.

Migration precondition for both: the capture guarantee already built for
variable templates must hold for class-scope alias bodies, plus a
member-context equivalent of the definition-lookup metadata replay installs
(`push_replay_member_context`). A strict post-substitution dependency check
(param-name identifiers, TemplateParameterReferenceNode, dependent call
records) is the right flip signal once those are fixed.

## Access control is still evaluated during IR generation, not sema

Member access control (`checkMemberAccess`, `checkMemberFunctionAccess`,
`isSameClassOrInstantiation` in `src/IrGenerator_MemberAccess.cpp`) runs at
AstToIr/IR-generation time; the parser and `SemanticAnalysis.cpp` perform no
access checking. Consequences: diagnostics lack real source locations (the
generic `"Access control violation"` `CompileError`), ill-formed accesses in
code that is never lowered are not diagnosed, and the accessing class context
is recovered from the codegen symbol table (`this` symbol) instead of
authoritative semantic class-scope state. The specialization-identity
comparison added for cross-specialization private access (canonical
`TemplateInstantiationKey` equality over stamped TypeInfo metadata) belongs
to that future sema-side checker. See "Main remaining gaps" entry 7 in
[SEMANTIC_ANALYSIS_STATUS.md](SEMANTIC_ANALYSIS_STATUS.md) for the migration
requirements.

## Template instantiation recursion has very high per-level stack cost

The recursive base-class instantiation path
(`try_instantiate_class_template` → `materializeTemplateInstantiationForLookup`
→ `instantiate_and_register_base_template`, plus the substitution machinery it
calls) consumes roughly 400KB of stack per inheritance level in `-O0` builds,
because the substitution functions keep many inline-storage locals on the stack.
A `Chain<39>`-style test needs ~17-20MB, which is why
`ensureMinimumProcessStackSize()` raises the Linux soft `RLIMIT_STACK` to 64MB
(src/FlashCppMain.cpp). The existing depth guard
(`kMaxTemplateInstantiationNestingDepth = 128`) only counts nesting levels and
cannot fire before the process stack is exhausted, so a chain roughly three to
four times deeper than `Chain<39>` would still overflow. Slimming the fat frames
(inline-storage locals in `substituteTemplateParametersWithState`,
`instantiate_and_register_base_template`) or converting fatal exhaustion into a
clean diagnostic would remove this ceiling.

## Current `<any>` / `<deque>` integration stops

The 2026-08-16 standard-header probes still fail outside this Phase 1 scope.
Both `tests/test_std_any.cpp` and `tests/test_std_deque.cpp` first report that
all template overloads for `_Seek_to` failed, followed by codegen diagnostics in
the MSVC `view_interface` implementation because `operator==` is unavailable.
These are generic constrained iterator/member lookup and operator-materialization
gaps; the compiler does not special-case those library names.

## Non-standard layout/constexpr acceptance gaps tracked as compatibility tests
These tests are intentionally kept in compatibility form so the current FlashCpp
suite stays green, even though they are not strictly standard-conforming under a
pedantic C++20 compiler.

- `tests/test_constexpr_offsetof_nested_ret0.cpp`
- `tests/test_constexpr_offsetof_ret0.cpp`
- `tests/test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`
- `tests/test_infer_expr_type_expansion_ret0.cpp`
- `tests/test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`
- `tests/test_outofline_nested_pack_ret0.cpp`
- `tests/test_outofline_nested_union_ret0.cpp`
- `tests/test_sizeof_offsetof.cpp`

## Constexpr evaluation does not yet expose standard semantic outcomes
The constant-expression pipeline still reports most unsuccessful evaluations as
one generic evaluator failure. It does not consistently distinguish these C++20
outcomes:

- the expression is still dependent and must be checked after substitution;
- substitution produced an ill-formed expression that requires a diagnostic;
- the expression is well-formed but is not a constant expression;
- the expression is a valid constant expression that the evaluator does not yet
  implement.

Template static-member normalization therefore still has phase-specific recovery.
`tryEarlyNormalizeTemplateStaticMemberInitializer(...)` returns no normalized
initializer for a generic evaluation failure, while `UnresolvedSizeofPolicy`
only makes unresolved/incomplete `sizeof(type-id)` a hard error during the final
retry for `constexpr` static members. C++20 validity is not determined by that
declaration flag or retry phase: a non-dependent invalid `sizeof` operand is
ill-formed whenever the specialization requires it.

The compatibility boundary is exercised by dependent NTTPs, recursive static
members, hidden-friend calls, and nested constexpr member/helper access, including:

- `tests/test_function_template_dependent_identifier_nttp_ret0.cpp`
- `tests/test_template_recursive_static_constexpr_member_ret0.cpp`
- `tests/test_template_static_constexpr_dependent_hidden_friend_ret0.cpp`
- `tests/test_template_static_member_initializer_helper_member_access_ret42.cpp`
- `tests/test_template_static_member_initializer_nested_constexpr_member_call_ret42.cpp`
- `tests/test_template_static_member_initializer_nested_helper_access_ret42.cpp`

These tests pass through the current staged replay/substitution/evaluation
pipeline, but a blanket conversion of every unsuccessful early evaluation into a
diagnostic regresses them. The long-term fix is a structured evaluation result,
standard point-of-instantiation checking for dependent expressions, and complete
constexpr call/member-access evaluation. Invalid semantic states should then
produce `CompileError`; missing canonical compiler metadata should produce
`InternalError`; unsupported evaluator coverage must not be accepted as either a
constant value or an ill-formed program.

## Partial-specialization member calls can select the non-const overload

Member-call lowering for a partial class-template specialization can select a
non-const member overload for a const receiver when otherwise-identical const and
non-const overloads are replayed. Exact injected-class return identity and mangling
are now preserved, so the call links correctly, but overload selection still needs
to retain and rank the receiver cv-qualification through replay and IR lowering.
Do not compensate by changing mangled names or treating the overloads as equivalent.

## SysV x87 aggregate return gap

Concrete SysV aggregate returns plan `direct` vs `indirect` from canonical layout
classification during IR generation, including single-eightbyte SSE-only values
such as `struct { float; }` / `struct { double; }` that must use XMM0 rather than
the legacy integer return path. Aggregates larger than two eightbytes, and ≤16-byte
MEMORY-class values such as unaligned packed aggregates, select a hidden return
slot. Incomplete or placeholder return types are `dependent` and must not be
treated as direct; concrete function/call IR requires a resolved plan and surfaces
missing canonical metadata as `InternalError`.

Aggregate returns containing `long double` remain on the legacy path. Their SysV
result classification uses the X87/X87UP classes (`%st0` / paired X87UP), which the
current backend return-register abstraction cannot represent yet. Aggregate
parameters containing `long double` are still classified as MEMORY as required.

This gap is specific to `long double` (not `float`/`double` SSE aggregates). Closing
it is blocked on broader `long double` codegen support. LLP64 sizing and literals
now consistently use the Microsoft x64 64-bit `double` representation, including
bit-preserving builtin bit-casts, but direct `long double` floating comparisons
still have an LLP64 lowering discrepancy. LP64 has type identity and some
constexpr/overload/builtin coverage, but no real x87 load/store/`%st0` emission
path. Constexpr evaluation often collapses
`long double` to `double`. Do not paper over return ABI with size guesses or INTEGER
fallbacks; wait until `long double` lowering can emit the SysV x87 convention.
