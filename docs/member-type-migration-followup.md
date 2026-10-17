# Agent prompt: finish the member TypeId migration

Review [PR #2396](https://github.com/GregorGullwi/FlashCpp/pull/2396) and continue
the member-type migration on top of `codex/publish-full-member-type-ids`.
Read `AGENTS.md`, `docs/MIGRATION_PROGRESS.md`, and `docs/KNOWN_ISSUES.md` first.
Use Luna agents for most implementation and investigation; coordinate and review
their work. Write a todo plan before architectural edits.

First inspect the PR's final validation status and finish any outstanding full
source-suite, native-suite, and standalone architecture checks. The six focused
source regressions passed before the final removal of redundant constexpr
wrappers. Compare complete-native-suite failures against the two documented
baseline failures before attributing them to this PR. Do not describe an
incomplete or baseline-failing run as fully green.

The wrap-up started the full source suite, writing
`x64/issue-member-full.log` and `x64/issue-member-full.jsonl`. A fresh native
build was still running when the handoff was prepared; check
`x64/issue-member-native-status.txt` for its eventual build/focused-test result.
Check for active processes before rerunning validation. The stack comparison
artifacts are under `x64/issue-member-baseline/stack/final-current/`.
Update the draft PR's validation paragraph with the final results, and mark it
ready only after reviewing outstanding failures and required checks.

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

The separately documented narrow `nullptr` store through `int*&` and inline
typedef-record function-pointer parser bug are not prerequisites for deleting
member TypeIndex storage. Keep their investigation separate unless a migrated
consumer directly requires a fix.

If investigating these final bugs first, add a regression before implementation:
the null-pointer assignment needs a destination-typed, full-width store rather
than a zero-literal width guess; inline typedef records should reuse the normal
declarator parser while preserving their declaration AST and layout. Avoid
reintroducing the rejected post-registration member-TypeId assignment pattern.
