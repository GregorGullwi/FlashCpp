# Agent prompt: finish the member TypeId migration

Review [PR #2396](https://github.com/GregorGullwi/FlashCpp/pull/2396) and continue
the member-type migration on top of `codex/publish-full-member-type-ids`.
Read `AGENTS.md`, `docs/MIGRATION_PROGRESS.md`, and `docs/KNOWN_ISSUES.md` first.
Use Luna agents for most implementation and investigation; coordinate and review
their work. Write a todo plan before architectural edits.

First inspect the PR's latest validation status. Compare complete-native-suite
failures against the two documented baseline failures before attributing them
to this PR. Do not describe an incomplete or baseline-failing run as fully green.

The Linux continuation fixes the two reported CI return mismatches: Itanium
symbols now distinguish pointer-reference qualification, and shared aggregate
member stores convert arithmetic values before storing the destination width.
Keep the source regressions for both cases and the native exact-encoding checks,
including their 4,096-level ordered and legacy pointer types.

The continuation's build, focused/full-suite, native, and stack artifacts are
under `x64/linux-takeover/`; the original stack comparison artifacts remain under
`x64/issue-member-baseline/stack/final-current/`. Check for active processes
before rerunning validation, and consult the PR for the final CI results.

The current slice publishes each supported member's full canonical `TypeId`
before construction. It preserves declared pointee and intermediate pointer CV,
resolved array bounds, specialization members, and receiver qualification.
Registration and layout rebuilds pass the ID as a required construction argument.
`StructMember::type_index` remains a compatibility base/category/layout handle.
It must not compete with the canonical ID as declared type identity.

Finish removing that compatibility dependency in reviewable slices:

1. Inventory readers of `StructMember` and `AnonymousUnionMemberInfo` type
   metadata, including differently named member variables. Cover layout,
   empty/no-unique-address members, nested lookup, constexpr initialization,
   templates, lambdas, indirect calls, aggregate stores, and ABI lowering.
2. Provide a canonical route from record, enum, and specialization IDs to the
   declaration/layout information each consumer needs. Preserve declaration and
   specialization identity; do not substitute interned spelling for identity.
3. Make unresolved/dependent and unsupported import outcomes explicit. Current
   member publication can retain an empty ID for those cases, and the legacy
   materializer still needs its TypeIndex. Resolve these boundaries before
   deleting the field; do not replace them with lossy reconstruction or guesses.
4. Migrate consumers to full member TypeIds and canonical queries. Derive any
   temporary compatibility projection at a named boundary. Remove redundant
   stored TypeIndex and flat pointer/CV/array metadata when their readers are
   migrated, retaining actual layout facts such as offsets and bitfield layout.
5. Keep member registration atomic. Avoid `addMember()` followed by writes to
   `members.back()` to finish type identity. Prefer one declaration-derived
   input over independently supplied identities that can disagree.

Add reduced regressions before fixes. Extend the member qualification,
publication-path, self/forward-reference, pointer-reference overload, and
pointer-array stride regressions added by this slice. Retain native tests for
4,096 pointer qualifiers and canonical reference binding. Mix native sizes,
records, references, arrays, aliases, and partial/full specializations. Negative
tests must encode exact diagnostic ID multisets in their filenames.

Do not introduce flat qualifier fields, library-name special cases, speculative
size fallbacks, native recursion for unbounded type/template depth, or larger
stack reserves. Measure memory and changed recursive-path frames against the
starting revision, and exercise deep cases under the normal stack limit.

Rebuild with `build_flashcpp.bat` after compiler changes, then run
`pwsh tests/run_all_tests.ps1`; never run those concurrently. Run the native
canonical fixtures and standalone canonical architecture checks too. Compare
any complete-native-suite failures with the documented baseline. Update the
migration documentation and create a PR describing the corrected behavior and
what the added regressions protect against.

The narrow `nullptr` store through `int*&` and inline typedef-record
function-pointer parser bugs are already fixed. Remaining nearby issues are
documented in `KNOWN_ISSUES.md`: local aggregate numeric conversion, nested
aggregate initialization, contextual bool conversion of `double*&`, and inline
typedef array-member publication. Keep these investigations separate from
deleting member TypeIndex storage unless a migrated consumer requires a fix.

## Reviewable TypeIndex consumer migration slices

`StructMember` (`src/AstNodeTypes_TypeSystem.h:1316`) and
`AnonymousUnionMemberInfo` (`src/AstNodeTypes_Template.h:1074`) each carry a
compatibility `TypeIndex` plus the canonical `TypeId`. The single compatibility
materializer is `typeSpecifierFromStructMemberProjection`
(`src/AstNodeTypes.cpp:3201`); its semantic twin is
`canonicalTypeDescFromStructMember` (`src/SemanticAnalysis.cpp:1201`). Existing
canonical queries live in `src/CanonicalTypes.h` (`recordLayout`,
`recordProperties`, `recordMemberAt`, `recordSubobjectMemberAt`,
`arrayDimensions`, class-base schema). `CanonicalRecordMember` deliberately
carries no spelling (`src/CanonicalTypes.h:341`). Migrate in this order; each
slice must be green on its own before the next begins.

0. **Atomic member construction (no field deletion).** Fold the remaining
   post-registration writes into one declaration-derived input: no
   `emplace_back(...)` followed by `members.back()` writes, and no
   post-registration `canonical_type_id` assignment
   (`src/Parser_Expr_BinaryPrecedence.cpp:2288-2306`,
   `src/Parser_Templates_Inst_ClassTemplate.cpp:13917-13919,14235-14237`).
   Regression: a callable member inside an anonymous union inside a
   specialization, plus `[[no_unique_address]]` and a bitfield, asserting offsets
   and signature survival.
1. **Publish the canonical schema directly from `canonical_type_id`.**
   `tryPublishCanonicalRecordFieldSchema` (`src/Parser_Decl_StructEnum.cpp:133-187`)
   currently re-derives the member type through the projection and bails for any
   anonymous-union group or unimportable member; publish from the canonical ID and
   handle anonymous-union groups. Publish
   `tryPublishCanonicalRecordSubobjectTypes` (`src/CanonicalTypeAdapter.h:1814`)
   at completion (it has no production caller today). Regression: native fixture
   asserting `hasRecordFieldSchema`/`recordMemberAt`/`hasRecordSubobjectTypes`
   for anonymous unions, partial/full specializations, and nested records.
2. **Canonical member-name lookup.** Add a `CanonicalTypeTable` query keyed by
   record `TypeId`/`EntityId` + interned name -> field-schema index + inherited
   offset (walk `publishClassBaseSchema`/`CanonicalClassBase.offset_bytes`).
   Names remain lookup keys, never identity. Publication + native test only; no
   consumer switch yet.
3. **Parser-side member-access typing.** Migrate the
   `typeSpecifierFromStructMemberProjection` callers in
   `src/Parser_Expr_QualLookup.cpp:56-73,4268-4281,4477-4491` to canonical export
   + canonical lookup; `memberTypeIsDependent` uses publication status rather
   than `type_index`. Extend `test_member_type_metadata_qual_ret0.cpp`; add a
   dependent/unresolved negative test with its exact diagnostic multiset.
4. **Semantic materializer canonical-only.** Make
   `canonicalTypeDescFromStructMember` and `memberPointerToArrayShape` derive
   entirely from `canonical_type_id` (export + `qualify`), with an explicit throw
   on an empty ID instead of flat reconstruction. Regression: extend the
   pointer-array stride and const-object-pointer-member copies.
5. **Constexpr initialization.** Migrate the Group D readers
   (`src/ConstExprEvalHelpers.cpp:1018-1058,1713-1755`,
   `src/ConstExprEvaluator_Core.cpp:434-457,920-935,7626-7631`,
   `src/ConstExprEvaluator_Materialize.cpp:700-830,1573-1576`,
   `src/ConstExprEvaluator_Arithmetic.cpp:112,177`). Regression: mixed native
   sizes, arrays, references, records, aliases, and partial/full specializations.
6. **IR member access, array stride, aggregates.** Migrate Group H and the
   member-array stride resolvers (`src/IrGenerator_MemberAccess.cpp:94-124,
   178-261,595-622`, `src/IrGenerator_Expr_Conversions.cpp:456,487-514,1310,
   1363,1381`, `src/IrGenerator_Expr_Primitives.cpp:493-500`,
   `src/IrGenerator_Visitors_TypeInit.cpp`,
   `src/IrGenerator_Visitors_Decl.cpp:3888-3909`,
   `src/IrGenerator_Stmt_Decl.cpp:2809-2828`,
   `src/IrGenerator_Expr_Operators.cpp:504-522`). Remove the hand-built synthetic
   `StructMember`. Regression: extend `test_member_pointer_array_reference_stride_ret0.cpp`.
7. **Indirect calls, lambdas/captures, mangling.** Migrate Groups F, G, I
   (`src/IrGenerator_Call_Indirect.cpp:770,803,1279`,
   `src/IrGenerator_Stmt_Control.cpp:663-669`, `src/IrGenerator_Lambdas.cpp:
   1236-1243`, `src/Parser_Expr_ControlFlowStmt.cpp:1215-1398`,
   `src/NameMangling.h:481-516,1470-1476`, `src/OverloadResolution.h:2099-2204`).
   Regression: extend `test_member_type_publication_paths_ret0.cpp`; add
   member-object-pointer mangling and lambda-capture member-type tests.
8. **Layout and emptiness canonicalization (last reader).** Introduce a canonical
   empty/layout fact queryable during layout and migrate `addMember`
   (`src/AstNodeTypes_DeclNodes.h:277-298`) and the empty-member walks
   (`src/AstNodeTypes.cpp:2529-2570,2949-2971`). This is the hard blocker: the
   enclosing record is still being laid out while these read the member's
   `TypeIndex`, and the member's `recordLayout` is published only at completion.
   Regression: extend `test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`
   with a deep-nesting bound check.
9. **Delete the compatibility storage.** Make publication status explicit
   (tri-state `Canonical`/`Dependent`/`Unsupported`) so an empty ID is never
   silently approximated, collapse the remaining fall-throughs to one named
   legacy boundary, then delete `type_index`, `pointer_depth`, `is_array`,
   `array_dimensions`, `pointee_array_declarator`, `member_cv_qualifier`,
   `memberType()`, and the `AnonymousUnionMemberInfo` equivalents, keeping actual
   layout facts (offsets, bitfields, alignment, referenced size). Negative test
   encodes the exact unresolved/dependent diagnostic multiset; rerun the
   migration counters within baseline.

Invariants throughout: atomic member registration; complete declared pointee and
intermediate pointer CV; resolved array bounds (`CanonicalTypeTable::arrayDimensions`,
unknown bound stays explicit); receiver qualification applied to the member object
via `qualify(member.canonical_type_id, receiver_cv)`; no flat qualifier fields,
no library-name special cases, no speculative size fallbacks, no native recursion
for unbounded type/template depth, no raised stack reserve; `StringHandle` is
spelling only; unresolved outcomes are explicit and fail closed.
