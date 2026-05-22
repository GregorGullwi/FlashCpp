# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-22

This document should stay forward-facing. It is not a historical ledger or
release log. Keep only the minimum completed-state context needed to explain
the current architecture and the next highest-impact work.

## Executive summary

FlashCpp now handles many practical C++20 template cases, but the main
remaining standards gap is still two-phase lookup across replayed template
infrastructure.

The highest-impact work is no longer broad parser cleanup. It is finishing the
owner-correct replay path for out-of-line member-function-template bodies that
interact with dependent bases.

## Current state

Useful assumptions before changing this area:

- the main template lookup paths already prefer semantic lookup records over
  raw parser heuristics;
- covered non-dependent template-body lookup already preserves
  definition-context binding;
- several replay-heavy paths now preserve enough metadata to reparse from
  source instead of depending on AST-only substitution;
- covered dependent owner/member-template chains already reuse a shared
  owner-aware materialization path;
- out-of-line constructor-template replay now preserves inner template metadata
  and matches renamed inner template parameters in the covered paths;
- nested out-of-line member-function-template replay now preserves
  instantiated outer parameter bindings while still copying definition-side
  parameter names.

Latest recorded full-suite validation:
`2502` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.

Latest focused replay regressions added on the current branch:
- `test_template_nested_ool_member_template_outer_param_binding_ret0.cpp`
- `test_template_ool_ctor_template_param_rename_replay_ret0.cpp`

## What is still wrong

### 1. Two-phase lookup and replay ownership are still incomplete

This is still the main conformance problem.

The remaining issue is not that replay exists. The issue is that some replay
paths still do not capture enough semantic metadata at parse time, so later
instantiation has to recover intent from partially substituted AST state.

The largest remaining uncovered surface is replayed out-of-line
member-function-template bodies that perform dependent-base lookup.

### 2. Dependent-name modeling is still too weak

`DependentQualifiedNameRecord` is useful, but it is still not a complete
`[temp.dep]` model. The remaining gaps matter mainly because they block the
replay/lookup work above.

### 3. Lower-priority work remains open

These are still incomplete, but they are not the next best use of effort unless
they directly block item 1:

- NTTP completion for the remaining C++20 categories;
- broader sema-owned deduction/ranking;
- final removal of repair-oriented fallback paths.

## Highest-impact next steps

1. **Finish out-of-line member-function-template replay through dependent bases**
   - This is the next bounded target.
   - Goal: replayed out-of-line member-function-template bodies should use the
     same owner-correct deferred-base lookup path already used by the covered
     inline and constructor-template cases.
   - Success condition: no AST-only fallback is needed for replayed
     `this->template ...` or equivalent dependent-base member-template lookup.

2. **Remove the next AST-only replay fallback**
   - After item 1, continue with the remaining declaration/static-member/
     deferred-base replay paths that still never captured enough replay
     metadata.
   - Prefer replay-first semantic attachment over adding more repair logic.

3. **Strengthen dependent-name/current-instantiation modeling only where it unblocks 1-2**
   - Expand richer dependent-base and unknown-specialization records only when
     required by the replay/lookup path above.
   - Avoid broad redesign work that does not directly reduce fallback behavior.

## Short completed-state summary

The following are complete enough to rely on:

- semantic lookup records back the main template lookup paths;
- covered non-dependent template-body lookup preserves definition-time binding;
- covered owner/member-template chains now stay on a shared owner-aware
  materialization path;
- out-of-line static-member replay preserves replay-visible template
  parameters in the covered paths;
- top-level out-of-line constructor-template replay preserves inner template
  metadata and reattaches deferred body/initializer-list state correctly in the
  covered paths;
- nested out-of-line member-function-template replay preserves instantiated
  outer parameter types while importing definition-side parameter names.

## Exit criteria

This audit can be retired when:

- the main replay paths no longer require AST-only fallback for valid template
  code;
- definition-time vs POI lookup timing is preserved across the main template
  body, member, static-member, and replay paths;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than reconstructed from placeholder spellings;
- ordinary overload ranking no longer materializes losing candidates by
  default.
