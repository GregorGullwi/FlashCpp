# Agent Prompt: Remove the legacy template lookup environment bridge

Investigate what is still required to remove `legacy_environment` from dependent template-argument lookup and replace it with a fully environment-driven model.

## Context
- Repository: FlashCpp
- Relevant code: `trySubstituteDependentTemplateArgForLookup()` in `src/ConstExprEvaluator_Members.cpp`
- Current state: `legacy_environment` is still live and cannot simply be deleted. It bridges older constexpr/member-evaluation paths that still populate `template_param_names` and `template_args` instead of a full `TemplateEnvironment`.

## Goals
1. Identify every path that still depends on the legacy name/arg arrays.
2. Migrate those paths to populate and consume `TemplateEnvironment` directly.
3. Remove the bridge only after behavior matches current standards-faithful substitution and lookup semantics.

## Constraints
- No platform-specific commands or tooling assumptions.
- Do not remove the bridge until all callers are migrated.
- Avoid fallback behavior that guesses missing bindings.
- Preserve current constexpr, member-evaluation, and dependent lookup behavior during the migration.

## Suggested investigation areas
- `src/ConstExprEvaluator_Members.cpp`
- `EvaluationContext`
- construction of `template_environment`
- any code that still reads `template_param_names` / `template_args`
- member-evaluation and constexpr replay paths

## Deliverables
1. A migration inventory of remaining legacy users.
2. A staged plan for converting them to `TemplateEnvironment`.
3. The implementation once all paths are covered.
4. Regression coverage for dependent member lookup, constexpr substitution, and nested template environments.
