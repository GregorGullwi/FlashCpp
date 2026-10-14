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
| _(none)_ | | |

All boundary-2F reproducers have been recovered; the last one,
`test_template_out_of_line_static_member_implicit_this_fail.cpp.txt`, now reports
`NonStaticMemberUseInStaticFunction` once the replayed out-of-line body surfaces
its structured rejection instead of a generic attachment failure.

The archive is intentionally indexed by source filename rather than by a
guessed diagnostic number. Recovery assigns the filename ID only after the
owning implementation path is stable and the exact emitted multiset is known.
