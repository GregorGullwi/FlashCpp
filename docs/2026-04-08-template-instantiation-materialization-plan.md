# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-28

This document tracks the current state of template instantiation, lazy
materialization, and the sema/codegen boundary. It is intentionally compact:
completed implementation history is summarized, and active work points at the
remaining concrete compiler gaps.

## Current State

The original template materialization plan is mostly closed.

- **Phases 0-4 are complete.** The original regression cluster remains covered;
  non-type template-argument identity has a canonical key path; alias-template
  materialization is centralized; late-materialized roots have an explicit
  register/normalize lifecycle; and dependent placeholder state is represented
  through `DependentPlaceholderKind`.
- **Phase 5 is complete in its intended ownership direction.** Sema now owns the
  first materialization of lazy member functions for the exercised ODR-use paths.
  Constructor, conversion-operator, direct-call, indirect-call, member-access,
  and struct-visitor slices were moved away from codegen-first materialization.
  Codegen's remaining lazy-member bridge delegates back to sema and is treated as
  a migration/diagnostic surface, not the desired first materializer.
- **Phase 6's concrete pack-deduction and pack-expansion reproducers are fixed.**
  Explicit and implicit function-template pack deduction now handles pack-before
  tail signatures, mixed explicit+deduced slices, multiple packs, packs not
  mapped to a function parameter pack, template-specialization element types
  such as `Box<Ts>...`, multi-dependent element types such as `Pair<Ts, Us>...`,
  nested wrappers such as `Wrap<Pair<Ts, Us>>...`, and class-template member or
  static-member function template cases. Fold-expression parsing/substitution
  now handles complex pack operands, and brace-initializer pack expansion works
  for explicit-size arrays, unsized arrays, and aggregate struct initialization.
- **The parser/template/sema boundary is substantially stronger.** The
  sema-owned post-parse surface rejects surviving `FoldExpressionNode` and
  `PackExpansionExprNode` forms; `SemanticAnalysis.cpp` no longer depends on
  direct `parser_.get_expression_type(...)` fallbacks; codegen fold/pack
  handlers are assertion-only; and late materialization has a pending
  semantic-root handoff.
- **`docs/KNOWN_ISSUES.md` no longer tracks template-pack materialization
  issues.** It currently records an unrelated floating-point array-subscript
  codegen bug.

## Recommended Next Steps

1. Pick one active fallback class from `docs/2026-04-27-fallback-comments-audit.md`
   and add a hard-fail probe or focused regression before changing behavior.
2. Prefer root-fixing metadata loss over deleting a fallback directly. Several
   probed fallbacks are active in the current corpus.
3. When a fallback is proven dead or root-fixed, delete the fallback and compact
   the audit note instead of adding another historical section here.
4. Validate template/compiler-source changes with `.\build_flashcpp.bat` and
   `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`.

## Landed Coverage

The following tests are representative guardrails for the now-closed template
materialization and pack-deduction work:

- `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
- `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
- `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp`
- `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`
- `tests/test_multi_pack_sizeof_deduction_ret0.cpp`
- `tests/test_implicit_deduction_box_pack_ret0.cpp`
- `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`
- `tests/test_multi_dep_pack_ret0.cpp`
- `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`
- `tests/test_nested_multi_dep_pack_ret0.cpp`
- `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`
- `tests/test_class_template_nested_pack_member_ret0.cpp`

## Validation baseline refreshed on 2026-04-23 (Phase 6: multi-dependent pack element type deduction)

Linux validation run after:
- Adding `function_pack_dependent_param_names` set to `CallArgDeductionInfo` in `Parser.h`.
- Populating the set with ALL dependent names in the pack element type in `buildDeductionMapFromCallArgs` (removing the early `break` that stopped at the first dependent name).
- Changing the `deduceTemplateArgsFromCall` variadic-Type gate from single-name equality to set-membership check.
- Fixing the inner box-unwrapping loop to find the position matching the current pack parameter name, enabling `Pair<Ts,Us>...` to extract `Ts` from position j=0 and `Us` from position j=1.
- Fixing `try_instantiate_single_template` pack-sizes population to use `function_pack_call_arg_end - function_pack_call_arg_start` for all packs in the set (not `template_args.size() - non_variadic_count`).
- Fixing the complex fold-expression substitution handler to prefer `pack_param_info_[0].pack_size` over `template_args.size() - non_variadic_count` for expansion count.
- Adding regression test `tests/test_multi_dep_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2186** compile+link+runtime passing tests (+1 from new `test_multi_dep_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**



Linux validation run after:
- Adding `function_pack_element_type_index` field to `CallArgDeductionInfo` in `Parser.h`.
- Setting `function_pack_element_type_index` in `buildDeductionMapFromCallArgs` when the pack element type is a resolved TypeIndex.
- Extending `deduceTemplateArgsFromCall` variadic type-param branch to extract inner types from template-specialisation pack call arguments using the stored TypeIndex.
- Adding regression test `tests/test_implicit_deduction_box_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2184** compile+link+runtime passing tests (+1 from new `test_implicit_deduction_box_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-23 (Phase 6: fold-expression complex-pack parser fix)

Linux validation run after:
- Extending fold-expression Pattern 1 `(... op cast-expr)` to accept non-bare-identifier pack operands in the parser.
- Extending fold-expression Pattern 3 `(init op ... op cast-expr)` similarly.
- Adding `FoldExpressionNode` constructor for binary fold with complex pack expression.
- Adding regression test `tests/test_fold_expr_complex_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2183** compile+link+runtime passing tests (+1 from new `test_fold_expr_complex_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

Landed on top of the drain-by-ODR-use pass:
- `SemanticAnalysis::drainLazyMemberRegistry` now calls `normalizeTopLevelNode(*result)` immediately after each successful `ensureMemberFunctionMaterialized` (in both the AST-walk pass and the ODR-use pass). This guarantees a freshly-substituted body has its internal call expressions annotated by sema (routed through `tryAnnotateCallArgConversions` → `tryMaterializeLazyCallTarget`). The normalize helpers dedup via `normalized_bodies_`, so re-normalizing an already-processed node is a safe no-op.

Deeper diagnosis of the residual 4 tests / 10 forwarder hits (unchanged count after the re-normalization change):
- The hits are always calls inside a freshly-materialized body (e.g. `helper()` / `other.method(...)` inside `Box<int>::compute()`).
- Instrumenting `tryMaterializeLazyCallTarget` reveals the real cause: when sema annotates those inner calls, it resolves them to the **template pattern's** declaration (`Box::helper`, `Box::method`), **not** to the instantiated struct's lazy stub (`Box$3ee5c699332008a6::helper`). The template pattern has its body, so `tryMaterializeLazyCallTarget` correctly short-circuits (`func_decl->get_definition().has_value() == true`). No lazy materialization is triggered at sema time, so the codegen forwarder is legitimately the first materializer when codegen lowers the instantiated receiver.
- This is a **substitution-layer gap**, not a materialization-layer gap: `ExpressionSubstitutor` does not rewrite intra-struct call targets to point at the instantiated struct's stubs. Fixing this properly means teaching the substitutor to redirect any `CallExprNode` whose resolved target is a member of the owning template pattern to the corresponding member of the instantiated struct (before sema runs), or alternatively teaching `tryMaterializeLazyCallTarget` to map a pattern-resolved call to the active instantiation via `member_context_stack_`.
- Scope of that fix is significantly larger than Slice G and deserves a separate slice (call it Slice H: "intra-instantiation call-target rewriting"). Until then, the codegen forwarder remains the correct and minimal bridge for these 10 hits.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G: ODR-use drain extension + audit log)

Landed on top of the Slice G foundation:
- `LazyMemberInstantiationRegistry::snapshotOdrUsedLazyEntries()` — returns a snapshot of `(owner, member, is_const)` triples for lazy entries currently marked ODR-used. Snapshot (not live view) because materialization mutates the map.
- `SemanticAnalysis::drainLazyMemberRegistry` gained a second fixpoint pass after the AST-walk pass that materializes the ODR-used residuals via `ensureMemberFunctionMaterialized`. Safe against SFINAE-probed instantiations by construction: they never flow through a sema annotation site that calls `markOdrUsed`.
- `AstToIr::materializeLazyMemberIfNeeded` gained a debug-level audit log (`Codegen:Debug`) that fires when the forwarder is still the first materializer for a lazy member.

Audit results (run against the full test corpus with `--log-level=Codegen:debug`):
- **4 tests, 10 first-materializer hits remain.** All hits are inner calls inside freshly-materialized bodies (e.g. calls to `method` / `helper` inside `Box<int>::compute()`'s body).
- Root cause: `drainLazyMemberRegistry` runs *after* `normalizePendingSemanticRoots`, so the freshly-substituted bodies created by the drain never get their internal `tryAnnotateCallArgConversions` pass. That means calls inside those bodies never reach `tryMaterializeLazyCallTarget`, never get `markOdrUsed`, and are left for the codegen-side forwarder to resolve on demand.
- This is the clear next architectural step: interleave sema annotation with the drain (either run `normalizePendingSemanticRoots` after the drain, or loop sema+drain to a fixpoint) so that newly-substituted bodies also get annotated before codegen runs. That would either eliminate the forwarder hits entirely or narrow them to an even smaller residual worth diagnosing.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline (unchanged by the drain extension):

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G foundation: explicit ODR-use plumbing)

Windows validation was re-run after landing:
- `LazyMemberInstantiationRegistry::markOdrUsed` / `isOdrUsed` / `odr_used_` set (with `...Any` variants and `clear()` integration) in `src/TemplateRegistry_Lazy.h`.
- `markOdrUsed` calls at three non-speculative sema sites:
  - `structHasConversionOperatorTo` in `src/SemanticAnalysis.cpp` (Slice A / tryAnnotateConversion path).
  - `tryMaterializeLazyCallTarget` in `src/SemanticAnalysis.cpp` (Slice B/C / direct + indirect call target).
  - `ensureSelectedConstructorMaterialized` in `src/SemanticAnalysis.cpp` (ctor selection).
- `ensureMemberFunctionMaterialized` intentionally left neutral — it is reachable from codegen / constexpr forwarders whose semantics are weaker than "sema proved ODR-use".

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

The foundation is in place for the drain-by-ODR-use extension to land as a separate follow-up commit (see step 2 under "Clear next steps").

## Validation baseline refreshed on 2026-04-22 (Slice F finalization; Phase 6 positional-fallback fix)

Windows validation was re-run after:
- reverting an attempted `try_instantiate_class_template` side-list tracking + drain extension that regressed `tests/test_namespaced_pair_swap_sfinae_ret0.cpp` (template arguments to SFINAE probes are instantiated in non-SFINAE context, so gating on `in_sfinae_context_` was insufficient — their member bodies are lazily ill-formed and must not be eagerly drained);
- removing the temporary audit instrumentation from `AstToIr::materializeLazyMemberIfNeeded`;
- rewriting the comments in `drainLazyMemberRegistry` and `materializeLazyMemberIfNeeded` to document the intentional scope of the drain and why the forwarder is retained;
- landing the Phase 6 positional-fallback fix in `try_instantiate_template_explicit` (gate on `has_variadic_func_pack`, skip pre-deduced call-arg slots) with new regression test `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected (+1 from new `test_explicit_variadic_pack_nondeduced_tail_fail.cpp`)
- overall result: **SUCCESS**

Slice F is complete in its landing form: the sema-owned drain is the first materializer for every struct reachable from `parser_.get_nodes()`, and the residual codegen forwarder is an on-demand bridge that delegates to sema rather than performing materialization itself.

## Validation baseline refreshed on 2026-04-21 (Slice F partial completion)

Linux validation was re-run after adding `SemanticAnalysis::drainLazyMemberRegistry` and collapsing `AstToIr::materializeLazyMemberIfNeeded` to a pure sema forwarder:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

A temporary audit guard was inserted in the forwarder that threw `InternalError` whenever sema was about to perform first-time materialization at codegen time. With only Slices A-E in place it caught 34 struct-visitor tests (confirming the struct-visitor was the remaining codegen-first site). After Slice F's drain was added, the same guard fell to 4 tests — all of them involving instantiated structs that live outside the top-level AST walk. Those residual tests are the only reason the forwarder still sees first-time work; they are the tracking item for step 3's outright deletion.

## Validation baseline refreshed on 2026-04-20 (Slice E completion)

Linux validation was re-run after removing the function-shaped deferred-queue fallback:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

An audit guard that converted the removed fallback into a hard `InternalError` was run against the full suite before deletion and was never triggered, confirming the fallback is genuinely unreachable with the new sema-first materialization at the queue-seeding sites.

## Related docs

- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`

## PR #1344 review comments addressed (2026-04-23)

Four review comments from PR #1344 were addressed:

### Fix 1: Tab/space indentation (Devin)
`src/Parser_Templates_Inst_Deduction.cpp` lines 1006-1012 had mixed leading
space + tab characters.  Replaced with pure tab indentation to match the rest
of the file.

### Fix 2: Namespace/template-specialisation pack name extraction (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — the pack-parameter name extractor
used `fp_type.token().handle()` unconditionally.  For simple `Ts... args` that
token correctly returns `Ts`, but for `MyBox<Ts>... args` it returns `MyBox`
(the template name), not the pack parameter `Ts`.

The fix: after reading the token handle, check whether it exists in
`tparam_nodes_by_name`.  If it does not (or is invalid), walk the `TypeInfo`
for the type specifier; if the type is a template instantiation, iterate its
`templateArgs()` and use the first entry whose `dependent_name` is valid as the
pack parameter name.  The stale `FLASH_LOG_FORMAT(Templates, Error, ...)`
diagnostic that fired for every template-instantiation type was removed because
it incorrectly flagged valid cases as errors.

A new test `tests/test_explicit_pack_template_specialization_ret0.cpp` was
added that calls `f<0>(a, b)` where `f` is declared
`template<int N, typename... Ts> int f(Box<Ts>... boxes)`.

### Fix 3: ScopeGuard ordering for exception safety (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — `ScopeGuard
restore_template_pack_sizes` was constructed *after* the loop that populates
`template_param_pack_sizes_`.  If the loop threw (e.g., `std::bad_alloc`) the
saved data would be permanently lost.  Moved the `ScopeGuard` construction to
immediately after the `std::move` and before the population loop.

### Fix 4: Empty pack falling through to naive fallback (Devin)
`src/Parser_Templates_Substitution.cpp` — when
`get_template_param_pack_size` returned `0` for an authoritative empty pack,
`found_variadic` was set to `true` but `num_pack_elements` stayed `0`, causing
the next `if (num_pack_elements == 0)` guard to re-enter the naive
`template_args.size() - non_variadic_count` formula.  This overcounts when
multiple variadic packs are present.  Changed the guard condition to
`if (num_pack_elements == 0 && !found_variadic)` so that an authoritative
empty-pack result is respected and the naive fallback is skipped.

### Validation baseline (2026-04-23)

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2182** compile+link+runtime passing tests (includes new test)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Phase 6 continued: inner member function pack in class-template context (2026-04-23)

Fixed the first entry in `docs/KNOWN_ISSUES.md` ("inner member function pack
in class template context — fold returns only first pack element").

### Root cause

When a class template contains an inner member function template with its own
variadic type pack — e.g.:

```cpp
template<typename... Ts>
struct Wrapper {
    template<typename... Us>
    int call(Us... args) { return (0 + ... + args); }
};
```

…the pattern `DeclarationNode` for `Us... args` on the member template did not
have `is_parameter_pack` set, and its type specifier's `token()` was empty
(the type is stored as a `TypeIndex` pointing to the template-parameter
`TypeInfo` for `Us`, not a named token).

Two downstream consequences:

1. `Parser::buildDeductionMapFromCallArgs` gated pack detection solely on
   `DeclarationNode::is_parameter_pack()` and so never populated
   `function_pack_call_arg_start/end`,
   `function_pack_dependent_param_names`, or
   `function_pack_template_param_name` for the inner pack.
2. `Parser::try_instantiate_member_function_template` had a naive
   per-template-param deduction loop that pushed exactly one
   `TemplateTypeArg` per template parameter, so the variadic `Us` was
   deduced with 1 element regardless of the call arity.

The combination caused `try_instantiate_member_function_template` to return a
single-element `Us = {int}` even for `w.call(10, 15, 17)`.  Downstream, the
MemberFunc parameter-expansion path only created `args_0` in the symbol
table, so `count_pack_elements("args")` returned `1` and the unary fold
`(0 + ... + args)` expanded to a single element.

### Fix

Two small, scoped changes:

- `src/Parser_Templates_Inst_Deduction.cpp / buildDeductionMapFromCallArgs`
  — detect packs by either (a) `is_parameter_pack()` or (b) the type
  specifier naming a variadic template parameter of the enclosing template
  (mirroring the fallback that already exists in
  `instantiate_member_function_template_core`).  Additionally, when
  extracting the pack's template-parameter name, fall back to the
  `TypeInfo::name()` when the type token handle is invalid, so that the
  inner pack's name (e.g. `Us`) is correctly recorded in
  `function_pack_dependent_param_names` /
  `function_pack_template_param_name`.
- `src/Parser_Templates_Inst_MemberFunc.cpp / try_instantiate_member_function_template`
  — handle variadic Type template parameters in the deduction loop by
  consuming the `function_pack_call_arg_start..function_pack_call_arg_end`
  slice (matching the variadic branch of `deduceTemplateArgsFromCall`),
  producing an empty pack for variadic template params that do not map to
  the function-parameter pack.

Regression test: `tests/test_class_template_inner_func_pack_fold_ret0.cpp`
exercises simple and multi-pack inner fold cases including a non-variadic
outer class template, verifying that all three inner-pack folds return the
expected sum.

### Validation baseline (2026-04-23 · afternoon)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2193** compile+link+runtime passing tests (+11 vs morning baseline; the
  additional passing tests are existing tests whose inner-pack fold or
  member-function pack deduction was previously incorrect)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Still open Phase 6 items (unchanged)

- ~~Static member function template via `S<T1,T2>::f(args...)` — body not
  queued for codegen; link fails with undefined reference.~~ (fixed — see below)
- Unsized array with pack-expanded initializer keeps pre-expansion size 1.
- Aggregate brace-init of a struct with pack-expanded initializer parse
  failure at the call site.

## Phase 6 continued: static member function template qualified call (2026-04-23)

Fixed the second entry in `docs/KNOWN_ISSUES.md` ("static member function
template — qualified call leaves body unmaterialized").

### Root cause (two interlocking bugs)

1. `parse_template_function_declaration_body` (in `Parser_Templates_Function.cpp`)
   parsed `static` via `parse_declaration_specifiers()` but only propagated
   `constexpr/consteval/constinit`.  The `StorageClass::Static` was silently
   dropped, so the template pattern's `is_static()` was false.  As a result
   `copy_function_properties` never set `is_static` on the instantiated
   function, causing codegen to emit an implicit `this` register save and
   shifting all real parameters by one (wrong ABI).

2. `try_parse_member_template_function_call` (in `Parser_Expr_QualLookup.cpp`)
   never called `try_instantiate_member_function_template` (the
   argument-deduction path) for qualified static calls.  It only tried
   `try_instantiate_member_function_template_explicit` (explicit template args
   or zero-arg case) and the lazy fallback (which handles non-template members
   only).  When call args were present but no explicit template args, no
   instantiation path was taken, leaving an undefined-reference at link time.

### Fixes

- `src/Parser_Templates_Function.cpp / parse_template_function_declaration_body`
  — after applying `constexpr/consteval/constinit`, also apply
  `StorageClass::Static` from `specs.storage_class`.
- `src/Parser_Expr_QualLookup.cpp / try_parse_member_template_function_call`
  — add a third branch (in addition to explicit-args and zero-args) that
  collects arg types via `get_expression_type` and calls
  `try_instantiate_member_function_template`.  This mirrors exactly the
  `Parser_Expr_PostfixCalls.cpp` handling for regular (non-static) member
  template calls.

Regression test: `tests/test_class_template_static_member_func_template_ret0.cpp`
covers variadic/non-variadic combinations of outer class template and inner
static member function template.

### Validation baseline (2026-04-23 · evening)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2194** compile+link+runtime passing tests (unchanged from the afternoon
  baseline; the static-member tests were already absent from the suite before
  this PR)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Still open Phase 6 items

- ~~Unsized array with pack-expanded initializer keeps pre-expansion size 1.~~ (fixed — see below)
- ~~Aggregate brace-init of a struct with pack-expanded initializer parse
  failure at the call site.~~ (fixed — see below)

## Phase 6 continued: pack-expanded brace initializers for unsized arrays and aggregate structs (2026-04-23)

Closed both remaining Phase 6 items in `docs/KNOWN_ISSUES.md`.  The file is now
empty (all previously-documented issues have been resolved).

### Root causes

1. **Unsized array `int arr[] = {args...}` kept pre-expansion size 1.**
   `Parser::inferUnsizedArraySizeFromInitializer` runs at parse time and sets
   the outer dimension from `init_list.initializers().size()`.  Before template
   substitution, the list contains a single `PackExpansionExprNode`, so the
   dimension was fixed at 1.  Although `substituteTemplateParameters` for
   `InitializerListNode` correctly expanded the pack into N substituted
   elements, the `DeclarationNode`'s `TypeSpecifierNode` still carried the
   stale `[1]` dimension, which `IrGenerator_Stmt_Decl.cpp` then preferred over
   the init-list size.

2. **Aggregate brace-init `Triple t = {args...};` rejected at parse time.**
   The positional aggregate-init branch of `parse_brace_initializer` (in
   `src/Parser_Statements.cpp`) called `parse_expression(2, Normal)` without
   checking for a trailing `...`, unlike `parse_brace_initializer_clause_list`
   (the array-path) which already wrapped the element in a
   `PackExpansionExprNode` when `...` followed.  The parser then either tried
   to parse `...` as the next positional initializer (failing) or emitted
   "Failed to parse initializer expression".

### Fixes

- `src/Parser_Templates_Substitution.cpp / VariableDeclarationNode branch`
  — after substituting the declaration and initializer, when the declaration
  is an unsized array and the substituted initializer is an
  `InitializerListNode`, emplace a fresh `TypeSpecifierNode` copy (to avoid
  mutating the pattern's shared spec for non-template element types like
  `int`) and re-run `inferUnsizedArraySizeFromInitializer` against the
  expanded initializer.  The `DeclarationNode::set_type_node` setter is used
  to swap in the updated spec.
- `src/Parser_Statements.cpp / parse_brace_initializer positional branch`
  — after `parse_expression`, detect a trailing `...` and wrap the element in
  a `PackExpansionExprNode`, mirroring the existing logic in
  `parse_brace_initializer_clause_list` and the array-element branch of
  `parse_brace_initializer`.  When a pack expansion is seen, advance
  `member_index` past the last member to suppress the parse-time
  "too many initializers" check (pack size is only known after substitution;
  downstream sema/IR-gen validates the expanded element count against the
  struct's member count).

Regression tests:
- `tests/test_unsized_array_pack_expansion_ret3.cpp` — exercises
  `int arr[] = {static_cast<int>(args)...}` and asserts
  `sizeof(arr)/sizeof(arr[0]) == 3`.
- `tests/test_aggregate_struct_pack_expansion_ret42.cpp` — exercises
  `Triple t = {static_cast<int>(args)...};` and asserts
  `t.a + t.b + t.c == 42`.

### Validation baseline (2026-04-23 · late evening)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2197** compile+link+runtime passing tests (+2 vs evening baseline from the
  new regression tests above)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Phase 6 status

With the two brace-init-pack-expansion issues closed, all items recorded in
`docs/KNOWN_ISSUES.md` as of this PR are resolved and the file is empty.  The
Phase 6 roadmap section above still lists "nested template packs and other
complex mapping shapes that may surface as the test corpus grows" as the
general open-ended follow-up, but there are no concrete reproducers tracked.

## Validation baseline refreshed on 2026-04-23 (Phase 6: nested wrapped pack deduction/materialization)

Validation run after:
- recursive nested dependent-name discovery / extraction in `src/Parser_Templates_Inst_Deduction.cpp`
- recursive nested placeholder materialization in `src/Parser_Expr_QualLookup.cpp`
- member-template pack-parameter materialization alignment in `src/Parser_Templates_Inst_MemberFunc.cpp`
- adding:
  - `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`
  - `tests/test_nested_multi_dep_pack_ret0.cpp`
  - `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`
  - `tests/test_class_template_nested_pack_member_ret0.cpp`

Validation commands:
- `clang++ -std=c++20 -fsyntax-only tests/test_nested_box_pack_implicit_deduction_ret0.cpp tests/test_nested_multi_dep_pack_ret0.cpp tests/test_explicit_nested_multi_dep_pack_ret0.cpp tests/test_class_template_nested_pack_member_ret0.cpp`
- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh test_nested_box_pack_implicit_deduction_ret0.cpp test_nested_multi_dep_pack_ret0.cpp test_explicit_nested_multi_dep_pack_ret0.cpp test_class_template_nested_pack_member_ret0.cpp test_implicit_deduction_box_pack_ret0.cpp test_multi_dep_pack_ret0.cpp test_explicit_multi_dep_pack_ret0.cpp test_class_template_inner_func_pack_fold_ret0.cpp test_class_template_static_member_func_template_ret0.cpp`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):
- **2201** compile+link+runtime passing tests (+4 from the nested wrapped-pack regressions)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**
