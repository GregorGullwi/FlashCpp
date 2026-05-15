# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) Variable-template template-id fallback still treated as callable in dependent static_assert

- **Symptom**: During `<string_view>` instantiation, expressions such as `is_same_v<...>` can fail with  
  `Identifier is not a function or callable object: is_same_v`.
- **Root cause**: When variable-template instantiation fails in dependent contexts, the evaluator still falls
  back to call-expression semantics instead of preserving a dependent value-template expression.
- **Affected path**: `ExpressionSubstitutor::substituteFunctionCallImpl` / variable-template instantiation +
  constexpr call-evaluation fallback.
- **Impact**: Blocks portions of libstdc++ headers that rely on variable templates in dependent checks.

## 2) Two-phase lookup for member function template bodies — **FIXED (2026-05-15)**

Two-phase name lookup (C++20 [temp.res]/9) is now applied inside member function template bodies.
Non-dependent calls inside `template <typename U> T Wrapper<T>::foo(U)` bodies now resolve against
definition-time overload sets, consistent with free function template bodies.

- **Fixed in**: `src/Parser_Templates_Inst_MemberFunc.cpp` — `instantiate_member_function_template_core`
  now sets `phase1_cutoff_line_`, `phase1_cutoff_file_idx_`, and
  `current_template_definition_lookup_context_` before `parse_function_body()`.
- **Regression guard**: `filterPhase1OrdinaryFunctionOverloads` is no longer called from
  `lookupMemberFunctionTemplateCandidatesForInstantiation` — member lookup within a class has
  no phase1 cutoff (all class members are mutually visible).
- **Regression test**: `tests/test_template_two_phase_member_func_template_ret42.cpp`

---

## 3) Out-of-line template member signature validation is still too strict for dependent aliases

- **Symptom**: Parser warnings like `Parameter 1 type mismatch in out-of-line template member ...` remain for
  valid declarations/definitions that differ only by dependent spelling/aliases.
- **Root cause**: `validate_signature_match` compares low-level type fields (`type_index`, category, pointer/ref)
  too rigidly for dependent contexts and does not use a more canonicalized/dependent-aware type equivalence.
- **Affected path**: `Parser::validate_signature_match`, invoked from out-of-line template member parsing.
- **Impact**: Noisy diagnostics and increased risk of picking/validating against suboptimal overload candidates.
