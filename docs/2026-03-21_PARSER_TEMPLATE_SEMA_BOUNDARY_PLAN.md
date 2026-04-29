# Parser / Template-Substitution / Sema Boundary Plan

**Last Updated:** 2026-04-29

## Status

Core boundary hardening is complete and still valid:

- `SemanticAnalysis.cpp` no longer relies on `parser_.get_expression_type(...)`.
- Pre-sema boundary checks exist for parser/template helper-node leakage.
- Fold/pack helper nodes are treated as invariant violations on sema/codegen
  surfaces where they should no longer survive.

## What is still relevant

This document now tracks only the remaining boundary-level work:

1. Keep post-parse boundary checks strict as new template features land.
2. Continue moving late-materialized roots through sema before downstream use.
3. Remove residual parser/template fallback paths only after proving metadata is
   preserved at the producing phase.

## What moved elsewhere

- Template/materialization roadmap:
  `docs/2026-04-08-template-instantiation-materialization-plan.md`
- Fallback inventory and probe outcomes:
  `docs/2026-04-27-fallback-comments-audit.md`
- Conversion/sema ownership summary:
  `docs/2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md`

## Guardrail

For sema-owned normalized bodies, missing required semantic metadata should be a
hard invariant failure (`InternalError`), not a codegen-time semantic recovery
path.
