# ExprResult Migration

**Status**: Completed (2026-03-13)  
**Related**: `docs/2026-03-12_IR_METADATA_STRONG_TYPES_PLAN.md`

## Outcome

The `ExprResult` migration is complete.

- The positional shim/bridge layer was removed.
- Consumers now use named `ExprResult` fields directly instead of relying on
  positional `ExprOperands`-style access.
- Helper paths that still preserve legacy encoded metadata were narrowed to the
  intentional compatibility bridge sites only.
- The follow-up strong-wrapper hardening work for `TypeIndex`,
  `PointerDepth`, and `SizeInBits` was completed separately and is documented in
  `docs/2026-03-12_IR_METADATA_STRONG_TYPES_PLAN.md`.

## Validation

Verified on 2026-03-13:

- `make main CXX=clang++` ✅
- `tests/run_all_tests.sh` ✅ (`1457` compile/link/runtime pass, `35`
  expected-fail correct)

## Remaining Work

No mandatory follow-up remains for the `ExprResult` migration itself.

Any future cleanup in this area is optional refinement work, not unfinished
migration scope.
