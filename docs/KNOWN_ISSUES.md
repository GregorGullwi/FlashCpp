# Known Issues

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
