# Agent Prompt: Restore full lazy-member identity in canonical owner lookup

Investigate and redesign lazy-member lookup keys so canonical lazy-member materialization does not collapse distinct overloads or template-ids onto the same key.

## Context
- Repository: FlashCpp
- Relevant review concern: `src/Parser_Templates_Inst_Substitution.cpp` and `src/TemplateRegistry_Lazy.h` currently use a lazy-member key that effectively tracks owner, member name, and constness, but not the full overload/template-id discriminator.
- This was judged valid but too large for the current PR because it requires a broader key redesign and registry migration.

## Goals
1. Preserve standards-faithful member lookup for overload sets and member template specializations.
2. Ensure lazy materialization keys uniquely identify the target declaration being materialized.
3. Avoid introducing fallback probing that guesses between overloads.

## Constraints
- No platform-specific commands or tooling assumptions.
- Maintain compatibility with existing lazy-member caches where possible, or provide a safe migration path.
- Do not reduce correctness to recover performance.

## Suggested investigation areas
- `Parser_Templates_Inst_Substitution.cpp`
- `TemplateRegistry_Lazy.h`
- lazy-member registration and lookup
- overload resolution interactions
- member template explicit-arg and const-qualified call paths

## Deliverables
1. A precise statement of what identity must be encoded in the key.
2. The key/registry redesign.
3. Tests covering overloaded members, member templates, and const/non-const distinctions.
4. Validation against the existing regression suite.
