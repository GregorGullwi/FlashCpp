# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-24 by branch `boundary1-diagnostic-engine`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Last landed slice: pull request boundary 1 item — bounded DiagnosticEngine
  foundation: stable numeric diagnostic IDs, severity, compact
  `SourceLocation`/`SourceRange`, structured arguments, attached notes,
  template-instantiation context storage, engine-owned accumulation inside
  `CompileContext`, engine-owned message and argument text, original-source
  line mapping, `CompileError` bridge preserving `what()`, four converted
  declarator-family diagnostic sites (including the silent double-`__asm`
  acceptance fix), machine-consumable `[Name#number]` rendered tags, and an
  always-available outside-engine counter that excludes speculative parser
  probes

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 0/78 (0%)
- Advanced, not completed:
  - Boundary 0 "internal invariant failures cannot be reported as success"
    and diagnosability machinery: diagnostic engine core and choke-point
    counters landed this slice; outstanding boundary-0 items are the ASAN
    crash investigation and giving the outside-engine counter its fixed
    corpus, baseline, and static guard

## Effort estimate

- Implementation effort completed overall: 2-3%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- None blocking. Runner mechanics can start consuming the `[Name#number]`
  diagnostic identity tags and `file:line:col:` prefixes immediately.

Then, in order:

1. Pull request boundary 0 completion: ASAN crash investigation; wire the
   outside-engine counter to a fixed corpus, recorded baseline, and static
   guard (removal boundary remains architecture boundary 11).
2. Pull request boundary 2: runner mechanics — diagnostic assertions over the
   `[Name#number]` contract, multi-TU and PIE modes, return-range validation,
   named expected-failure manifest with stale-entry detection.
3. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated.

Named follow-ups carried forward:

- Unify the ParseResult-channel pointer-to-reference twin at
  `src/Parser_Decl_DeclaratorCore.cpp:477` onto `DiagnosticId::
  PointerToReferenceType` once ParseResult carries structured diagnostics;
  converting it today would turn recoverable declarator probing into throws.
- Blanket `noexcept` on member functions stays deferred until boundaries 5-8
  shrink the exception surface to invariant-only paths.

## Active findings

Current findings only; delete entries when their resolution lands.

- The unity doctest target's MSBuild ClangCL configuration crashes the clang
  frontend against the VS18 STL headers (LLVM 20.1 vs STL 14.51 mismatch).
  Unit tests are validated through the direct LLVM clang-cl driver instead.
  Resolution home: toolchain alignment of `tests/FlashCppTest`.
- Pre-existing unity-suite failure
  `SemanticAnalysis:*QueryTracksAnalysisState` reproduces on clean `main`;
  details and suspected shared-static cause live in docs/KNOWN_ISSUES.md.
  Owner: sema query lifecycle.
