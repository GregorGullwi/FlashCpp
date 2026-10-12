# Boundary 2F archived negative reproducers

These `.cpp.txt` files preserve negative reproducers that were removed from
the executable test corpus on 2026-08-30. They are not discovered by either
test runner. The authoritative migration context is
`docs/MIGRATION_PROGRESS.md`; the stop-rule decision and the current blockers
are recorded in `docs/KNOWN_ISSUES.md`.

## Recovery workflow

An agent looking for a regression to recover should:

1. Start with the table below and select the row whose blocked owner matches
   the code being changed.
2. Read the corresponding `.cpp.txt` file and reproduce its current behavior
   with the compiler before changing the test.
3. Confirm that the fix provides a stable diagnostic at the named owner. Do
   not add parser recovery, identity recovery, replay, or a generic diagnostic
   solely to make an archived test pass.
4. Rename the file to an encoded `_e<ID>` test, move it back to the root
   `tests/` directory, and add or update the focused regression coverage for
   the fixed behavior.
5. Run the full negative contract suite and migration counters before removing
   the matching row from this index.

## Index

| Reproducer | Blocked owner | Recover when |
| --- | --- | --- |
| `concept_error_test_fail.cpp.txt` | Template constraint evaluation | Constraint failure has a stable existing diagnostic owner. |
| `template_call_wrong_placeholder_base_fail.cpp.txt` | Template placeholder/base deduction | Placeholder/base matching has a bounded rejection owner. |
| `template_concrete_undeduced_fail.cpp.txt` | Explicit template deduction | The undeduced-argument path reports a stable diagnostic. |
| `test_const_rvalue_reference_before_pack_lvalue_fail.cpp.txt` | Function-template deduction | Reference/pack deduction has a stable mismatch owner. |
| `test_constrained_auto_double_fail.cpp.txt` | Constrained abbreviated template | Constraint rejection has a stable bounded owner. |
| `test_function_template_recursive_trailing_return_fail.cpp.txt` | Recursive trailing-return instantiation | Recursive substitution terminates through a stable rejection owner. |
| `test_template_out_of_line_static_member_implicit_this_fail.cpp.txt` | Out-of-line static-member materialization | Static-member implicit-`this` validation has a stable owner. |

The archive is intentionally indexed by source filename rather than by a
guessed diagnostic number. Recovery assigns the filename ID only after the
owning implementation path is stable and the exact emitted multiset is known.
