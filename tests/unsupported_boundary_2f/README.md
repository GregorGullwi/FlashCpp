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
| `test_constexpr_aggregate_brace_narrowing_fail.cpp.txt` | Constexpr aggregate initialization | Narrowing is reported by the existing initialization owner without status-2 compatibility. |
| `test_constrained_auto_double_fail.cpp.txt` | Constrained abbreviated template | Constraint rejection has a stable bounded owner. |
| `test_function_template_recursive_trailing_return_fail.cpp.txt` | Recursive trailing-return instantiation | Recursive substitution terminates through a stable rejection owner. |
| `test_if_constexpr_active_branch_invalid_fail.cpp.txt` | `if constexpr` active-branch validation | The active branch is validated through its existing semantic owner. |
| `test_injected_identity_ool_namespace_owner_fail.cpp.txt` | Out-of-line injected identity | Namespace/owner identity is preserved without recovery heuristics. |
| `test_injected_identity_other_specialization_default_fail.cpp.txt` | Injected identity and default arguments | Specialization identity is stable through default-argument substitution. |
| `test_injected_identity_terminal_member_template_fail.cpp.txt` | Injected identity and member templates | Terminal member-template identity has a bounded owner. |
| `test_mismatch_args_fail.cpp.txt` | Declaration parser fallback | Shared declaration dispatch preserves the originating mismatch. |
| `test_mismatch_const_fail.cpp.txt` | Declaration parser fallback | Const-mismatch validation is owned before expression fallback. |
| `test_mismatch_return_fail.cpp.txt` | Declaration parser fallback | Return mismatch is reported at declaration validation. |
| `test_operator_subscript_const_ambiguity_fail.cpp.txt` | Const operator overload resolution | Ambiguity is reported by the existing operator owner. |
| `test_pointer_const_mismatch_fail.cpp.txt` | Declaration parser fallback | Pointer-qualification mismatch has a stable declaration owner. |
| `test_reference_const_mismatch_fail.cpp.txt` | Declaration parser fallback | Reference-qualification mismatch has a stable declaration owner. |
| `test_template_callable_operator_const_receiver_explicit_member_fail.cpp.txt` | Callable member-template substitution | Const-receiver substitution has a stable call owner. |
| `test_template_lazy_static_member_implicit_this_fail.cpp.txt` | Lazy member materialization | Implicit-`this` validation is stable without internal-failure compatibility. |
| `test_template_member_call_const_receiver_fail.cpp.txt` | Const member-call substitution | The existing member-call owner reports the failure. |
| `test_template_member_func_template_const_ref_return_fail.cpp.txt` | Declaration parser fallback | The member-template return mismatch is preserved by declaration dispatch. |
| `test_template_nested_ool_ctor_template_alias_target_mismatch_fail.cpp.txt` | Out-of-line constructor identity | Nested constructor/alias identity has a stable owner. |
| `test_template_ool_member_template_single_candidate_alias_target_mismatch_fail.cpp.txt` | Out-of-line member-template identity | The single-candidate alias target mismatch is directly diagnosed. |
| `test_template_ool_plain_member_multi_param_late_mismatch_fail.cpp.txt` | Out-of-line member deduction | Late multi-parameter mismatch has a bounded deduction owner. |
| `test_template_ool_plain_member_single_candidate_alias_target_mismatch_fail.cpp.txt` | Out-of-line member identity | Single-candidate alias matching is stable. |
| `test_template_out_of_line_static_member_implicit_this_fail.cpp.txt` | Out-of-line static-member materialization | Static-member implicit-`this` validation has a stable owner. |
| `test_template_partial_spec_ool_ctor_template_alias_target_mismatch_fail.cpp.txt` | Partial-specialization identity | Partial-specialization constructor identity is preserved. |
| `test_template_partial_spec_ool_plain_member_alias_target_mismatch_fail.cpp.txt` | Partial-specialization member identity | Partial-specialization member matching has a bounded owner. |

The archive is intentionally indexed by source filename rather than by a
guessed diagnostic number. Recovery assigns the filename ID only after the
owning implementation path is stable and the exact emitted multiset is known.
