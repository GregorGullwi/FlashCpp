# Template Instantiation / Constexpr Architecture Audit

**Date:** 2026-04-06  
**Last Updated:** 2026-04-29

## Current relevance

This document is still relevant as a **status snapshot**, not as an active
multi-phase implementation plan.

The core audit conclusion still matches the current code:

- eager parse -> sema -> codegen is established;
- late/lazy instantiation remains the main pressure point;
- constexpr still contains some fallback-style recovery paths.

## What has changed since the original audit

The original long workstream checklist is mostly superseded by:

- `docs/2026-04-08-template-instantiation-materialization-plan.md` for the
  active template/materialization roadmap;
- `docs/2026-04-27-fallback-comments-audit.md` for fallback-site status and
  probe results.

## Verified codebase state (2026-04-29)

- `SemanticAnalysis.cpp` no longer calls `parser_.get_expression_type(...)`.
- Codegen still contains fallback-oriented type reconstruction via
  `buildCodegenOverloadResolutionArgType(...)` (`IrGenerator_Stmt_Decl.cpp` and
  users).
- Parser/constexpr/template code still has remaining fold/pack and
  late-materialization complexity; boundary enforcement is stronger than before,
  but not fully "no-fallback" end-state.

## Keep / compact policy

Keep this file as a short architectural context note.  
Do **not** add new phase logs here. Record active work in:

1. `docs/2026-04-08-template-instantiation-materialization-plan.md`
2. `docs/2026-04-27-fallback-comments-audit.md`
3. `docs/2026-04-04-codegen-name-lookup-investigation.md` (for sema/codegen ownership gaps)
