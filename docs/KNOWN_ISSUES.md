# Known Issues

## Declaration-parse errors are masked by the expression-statement fallback

When `parse_function_declaration` returns a `ParseResult` error, the top-level
declaration dispatcher restores the saved token position and retries the line
as an expression statement. The fallback's own error then replaces the
originating one in terminal output. Example: the `[dcl.fct.default]/4` check
(`Parser_FunctionHeaders.cpp`) produces `Missing default argument on parameter`
for `(int a = 10, int b)`, but the surfaced message is the fallback's
`Unexpected keyword in expression context`. Consequences for boundaries 2C-2F:
converting such a `ParseResult` site to a structured ID would pin the fallback's
diagnostic, not the rule's. A masked site must first route its rejection
through the shared declaration/expression dispatch, or its test is deleted
under the incomplete-feature rule (`test_default_arg_after_nondefault_fail.cpp`
was deleted for exactly this reason). Owner: parser declaration dispatch.

## Constant-expression integer arithmetic is evaluated in 64-bit

`ConstExprEvaluator` models integer operands as 64-bit values, so signed
32-bit overflow inside a constant expression (for example
`constexpr int x = 2000000000 + a;`) wraps silently instead of being rejected
under C++20 [expr.const]/4. `DiagnosticId::ConstantExpressionSignedIntegerOverflow`
(1205) is already wired at the signed-arithmetic fault sites but is currently
unreachable for 32-bit overflow; it fires only at the `long long` boundaries.
Pin it with an encoded regression once the evaluator tracks promoted operand
widths. Owner: constexpr evaluation fidelity.

## Five legacy negative tests terminate through internal-failure paths

Boundary 2A gives clean source rejection exit status 1 and internal/compiler
failure exit status 2. The frozen negative suite now exposes five tests that
previously passed only because the runner treated any missing object as an
acceptable rejection:

- `test_constexpr_aggregate_brace_narrowing_fail.cpp` records the intended
  constexpr narrowing error, then hits `sema missed return conversion`;
- `test_if_constexpr_active_branch_invalid_fail.cpp` reaches a no-runtime-size
  direct-call return invariant;
- `test_operator_subscript_const_ambiguity_fail.cpp` reaches the
  struct-without-conversion-operator fallback invariant;
- `test_template_lazy_static_member_implicit_this_fail.cpp` reaches code
  generation with an invalid implicit `this` lookup;
- `test_template_out_of_line_static_member_implicit_this_fail.cpp` reaches the
  same invalid code-generation lookup.

Boundary 2A tracks these names in the immutable
`tests/legacy_internal_failure_tests.txt` inventory. A still-present original
`_fail.cpp` in that list may temporarily satisfy its legacy negative contract
with internal status 2 only when the compiler produces no object. The exception
does not apply to encoded successors or any other legacy test. Crashes,
timeouts, driver or worker failures, and missing results still fail.

Both runners report the active count against baseline 7. The count may only
fall. Their shared diagnostic owners belong to the bounded 2C through 2F
conversion slices, and boundary 2F deletes this compatibility after the last
owner migrates.

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
Cast type-ids also accept parenthesized abstract-declarator groups:
`static_cast<int(*)[3]>(v)`, `static_cast<const int(*)[3]>(v)`,
`reinterpret_cast<long(*)[2][4]>(v)` and the C-style `(int(*)[3])v` route the
group through `parse_declarator` via `consume_cast_type_id_paren_declarator`,
so the pointee shape matches the named spelling `int (*p)[3]`
(tests/test_ptr_to_array_cast_type_id_ret0.cpp).
Remaining gaps in the same area:

- Indexing through a class-template-instantiated pointer-to-array member
  (`Box<int> b; b.cells = &arr; (*b.cells)[i][j];`) crashes at runtime. The
  non-template struct path is fully working
  (tests/test_ptr_to_array_member_subscript_ret0.cpp); the instantiated-
  member case loses the pointee bounds somewhere in template substitution /
  lazy member resolution and falls back to non-flattened subscripting with a
  bad base.

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

## Implicit default-constructor deletion misses const scalar members

`StructMember` does not currently preserve the declared cv-qualification of a
non-static data member. Consequently, semantic special-member finalization can
detect reference members and class-type subobjects that delete an implicit
default constructor, but cannot yet implement every C++20
`const-default-constructible` rule for a const non-class member without a default
member initializer (for example, `struct S { const int value; };`). Preserve
member cv metadata through parsing and template substitution, then make the
sema-owned implicit default-constructor record decide this case. Do not recreate
the decision in IR from type spellings.

## Deferred alias bases can leave an incomplete default-constructor plan

Some deferred class-template base aliases retain the source alias spelling and
an unresolved `TypeIndex` after the enclosing specialization is otherwise ready
for IR. Sema records this explicitly as
`ImplicitDefaultConstructorSemanticRecord::has_unresolved_base_initialization`,
but cannot yet publish a concrete base-constructor action or determine deletion
from that base. Preserve the resolved base declaration identity during alias
substitution, then require semantic special-member finalization to resolve every
base. Codegen rejects an incomplete plan; do not restore name lookup or silently
omit the unresolved base action.

## Recursive class-template constant chains can overflow the native stack

A generated benchmark probe using a recursively specialized class template
whose static constant references `DepthValue<N - 1>::value` overflowed the
shipping Windows compiler stack at shallow logical depth. The crash recursed
through class-template materialization and expression substitution rather than
producing an implementation-limit diagnostic. The throughput corpus avoids
this construct; the query benchmark retains a separate 1,025-level logical
dependency probe. Architecture boundary 7 must move the real instantiation and
substitution path onto small arena-owned frames before this issue can be closed.

## SemanticAnalysis query-state doctest fails on a clean tree

The unity doctest build (tests/FlashCppTest) fails
`SemanticAnalysis:ResolvedDirectCallQueryTracksAnalysisState` on clean `main`
as of 2026-08-24: an expression-type query reports `Available` before
`SemanticAnalysis::run()`, so `before_run.state == NotYetAnalyzed` does not
hold. Reproduced with the LLVM clang-cl 20.1 unity build from the repository
root; unrelated to the DiagnosticEngine slice that surfaced it. Suspect shared
static state across earlier TEST_CASEs in the same process. Owner: sema query
lifecycle; fix by isolating per-test semantic state or resetting query slots.

## ELF preprocessing also defines Windows target macros

FlashCpp currently defines `_WIN32`, `_WIN64`, and `_MSC_VER` even for its LP64
ELF target, where it also defines `__ELF__`. Portable source cannot use `_WIN32`
alone to distinguish the generated object format. The Win64-only virtual ABI
regression therefore uses `__ELF__` as its target guard.
