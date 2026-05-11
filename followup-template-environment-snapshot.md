# Agent Prompt: Remove quadratic template-environment snapshot flattening

Investigate and redesign template-environment snapshot storage so nested template instantiations do not copy all parent bindings into every child snapshot.

## Context
- Repository: FlashCpp
- Relevant review concern: `src/TemplateEnvironment.cpp` currently flattens `TemplateEnvironmentSnapshot` by copying parent bindings into each new snapshot, which causes O(N^2) total binding storage across deep template nesting.
- This was judged valid but too large for the current PR because it affects environment persistence, replay, and lookup behavior across the compiler.

## Goals
1. Preserve existing C++20-correct lookup behavior for nested template environments.
2. Reduce snapshot storage and construction cost so deep template hierarchies do not duplicate parent bindings repeatedly.
3. Keep legacy views and replay paths working until they can be migrated safely.

## Constraints
- No platform-specific commands or tooling assumptions.
- Prefer architectural fixes over caching hacks.
- Do not silently change lookup precedence or pack behavior.
- Keep behavior compatible with existing parser, constexpr, and instantiation paths.

## Suggested investigation areas
- `TemplateEnvironment.cpp`
- `TemplateEnvironmentSnapshot`
- snapshot serialization / deserialization helpers
- any code that assumes snapshots are already flattened
- lookup sites that walk parent chains vs stored snapshots

## Deliverables
1. A short design note explaining the chosen representation.
2. The implementation.
3. Regression coverage proving nested template lookup still works.
4. Before/after validation on the existing build and test workflow.
