# Template Instantiation Identity / Materialization Follow-up Plan

**Date:** 2026-04-08  
**Last Updated:** 2026-04-14  
**Context:** Follows the branch fix that made `test_integral_constant_comprehensive_ret100.cpp`, `test_integral_constant_pattern_ret42.cpp`, `test_ratio_less_alias_ret0.cpp`, `test_sfinae_enable_if_ret0.cpp`, and `test_sfinae_same_name_overload_ret0.cpp` pass by preserving dependent non-type template-argument identity in template-instantiation keys.

## Current snapshot

- **Phase 0:** effectively complete; keep the original regression cluster as the must-pass guardrail.
- **Phase 1:** complete. Non-type template-argument identity now has one canonical key path.
- **Phase 2:** **partially complete**. Several alias/materialization bugs are fixed, but there is still no single authoritative alias-template materialization service.
- **Phase 3:** not started.
- **Phase 4:** not started.
- **Phase 5:** intentionally blocked until the Phase 2-4 prerequisites are in place.
- **Current Linux validation baseline:** `make main CXX=clang++` and `bash ./tests/run_all_tests.sh` currently pass with **2092 pass / 139 expected-fail**.

## What has already landed

### Phase 1 completed

- `NonTypeValueIdentity` is now the canonical value-identity carrier used by `ValueArgKey`.
- Hashing, equality, stringification, and instantiation-key generation now project from the same non-type identity path.
- The original dependent-vs-concrete non-type template-argument collision is fixed.

### Phase 2-adjacent cleanup completed

- Alias target capture is partially centralized via `parseRawAliasTargetTemplateId(...)`.
- Qualified/global alias-template targets are handled more consistently.
- Constructor-template deduction no longer creates persistent dummy AST wrappers.
- `materializeMatchingConstructorTemplate(...)` now returns `nullptr` on failure instead of forwarding an unusable template ctor.
- Direct-init and constructor-expression pack expansion now mirror the ordinary function-call path.
- `try_instantiate_member_function_template(...)` no longer falls back to `arg_types[0]` for undeduced parameters.

### Constructor-materialization fixes already landed

- Template constructor member-initializer lists are parsed and stored.
- `SemanticAnalysis::tryAnnotateInitListConstructorArgs(...)` now performs template-constructor materialization, matching the constructor-call sema path.
- Lazy template constructor body re-parse now uses full member-function context (`FunctionParsingScopeGuard`), so common template constructors no longer fall through to the old noop codegen safety net.

### Phase 6-side deduction fixes already landed

- Shared deduction mapping via `buildDeductionMapFromCallArgs(...)` was expanded.
- Variadic/non-variadic mixed deduction improved in the explicit-template path.
- `appendFunctionCallArgType(...)` now handles several compound expression forms that previously defaulted to `int`.

## What is still open before Phase 3 can start cleanly

Phase 3 is about making late materialization + pending-sema normalization an explicit contract. To start that work without re-baking parser duplication into the new queue/contract, the remaining **Phase 2 consolidation** should be narrowed first:

1. **Create one authoritative alias-template materialization helper**
   - input: alias template identity + args + use-site context
   - output: structured resolved/materialized result, not just a name string
2. **Route all remaining alias-template entry points through it**
   - top-level `using`
   - struct-local `using` / typedef
   - general type-specifier alias handling
   - substitution / `ExpressionSubstitutor.cpp` paths
3. **Remove the last duplicated helper logic around alias materialization / placeholder argument handling**
   - `ExpressionSubstitutor.cpp` dead fallback / duplicated name-resolution path
   - duplicated substitution+evaluation patterns
   - `materialize_placeholder_args` extraction if it is still needed to support the shared path
4. **Re-audit late-materialized alias/class instantiation sites** so Phase 3 can convert them to one registration/enqueue contract instead of preserving today’s special cases.

**Practical read:** the constructor side is far enough along now; the remaining blocker before Phase 3 is mostly alias/materialization centralization, not another constructor bug.

## What is still open before Phase 5 should start

Phase 5 is the broader parser/sema ownership move. It should not begin until the smaller representation/queue cleanups are done.

### Required before Phase 5

1. **Finish the remaining Phase 2 centralization** listed above.
2. **Complete Phase 3** so late materialization has one explicit register/enqueue/normalize contract.
3. **Complete Phase 4** so unresolved-dependent placeholder states are represented explicitly rather than inferred from names.
4. **Shrink the remaining codegen-triggered template materialization surface** so Phase 5 is moving ownership, not debugging mixed ownership:
   - `IrGenerator_Stmt_Decl.cpp` still retains fallback calls into parser materialization
   - sema still does not cover every constructor-call shape under one enforced invariant
   - codegen-side lazy-instantiation bridges still exist as safety nets

### In other words

- **Before Phase 3:** finish consolidating alias/materialization entry points.
- **Before Phase 5:** finish Phase 3 and Phase 4, then remove or quarantine the remaining codegen fallback materialization paths so ownership can move intentionally.

## Target invariants

After the remaining work:

1. A non-type template argument has **one canonical identity model** from parser capture through registry lookup and instantiated-name generation.
2. Alias-template uses are materialized through **one authoritative helper**, not hand-reimplemented at each parser entry point.
3. Any late-materialized AST root that becomes visible to sema, constexpr, or codegen goes through **one explicit registration + normalization path**.
4. Unresolved dependent placeholders are identified by **typed state**, not by best-effort name inspection.
5. Codegen only consumes already-materialized constructor declarations; it does not decide when template constructors get instantiated.

## Short bottom line

The document no longer needs another long historical audit pass before the next architecture work. The remaining path is straightforward:

- finish the last real **Phase 2** duplication around alias-template materialization,
- then do **Phase 3** as the explicit late-materialization/sema contract,
- then do **Phase 4** explicit placeholder state,
- and only after that start **Phase 5** to remove the remaining parser/codegen ownership blur.

---

## Concrete implementation plan

## Phase 0: freeze the regression surface first

**Goal**

Keep the recent metaprogramming fixes stable while refactoring the underlying representation.

**Primary files**

- `tests\test_integral_constant_comprehensive_ret100.cpp`
- `tests\test_integral_constant_pattern_ret42.cpp`
- `tests\test_ratio_less_alias_ret0.cpp`
- `tests\test_sfinae_enable_if_ret0.cpp`
- `tests\test_sfinae_same_name_overload_ret0.cpp`

**Concrete work**

1. Treat the five restored tests as a must-pass regression set for every slice below.
2. Add one or two narrow regression tests if a phase changes representation without exercising an existing failure shape:
   - alias chain with dependent non-type argument
   - late-instantiated alias-template use reached through constexpr or qualified lookup
3. Keep the refactor incremental: do not combine Phase 1 and Phase 2 into one large patch.

**Done when**

- every phase can be validated against the current five-test regression set,
- any new failure can be localized to one architectural slice rather than a multi-file rewrite.

---

## Phase 1: canonicalize non-type template-argument identity

**Status:** COMPLETE (as of 2026-04-12)

**Completed work:**
1. ✅ Introduced `NonTypeValueIdentity` carrier struct in `src/TemplateTypes.h`
   - Added `value_type_index` field to capture full type identity for value args (not just `TypeCategory`)
   - Added factory methods: `makeConcrete()`, `makeDependent()`
   - Added `toString()` for debugging/name generation  
   - Added `operator==` with Bool/Int interchangeability
   - Added `hash()` consistent with equality
2. ✅ Made `ValueArgKey` an alias for `NonTypeValueIdentity` for backward compatibility
3. ✅ Added `valueIdentity()` accessor to `TemplateTypeArg` 
4. ✅ Updated `makeInstantiationKey()` to use `valueIdentity()` accessor
5. ✅ Updated `TemplateTypeArg::toString()` to delegate to `NonTypeValueIdentity::toString()`
6. ✅ Updated `TemplateTypeArg::hash()`, `toHashString()`, and `TemplateTypeArgHash` to share one hash path
7. ✅ Added canonical `TemplateTypeArg::makeDependentValue(...)` helpers and reused them in the main deferred/dependent non-type argument materialization paths
8. ✅ All 2052 tests pass, 132 expected-fail

**Deferred follow-up (not required to consider Phase 1 complete):**
- Removing redundant `is_value + value + is_dependent + dependent_name` storage from `TemplateTypeArg` is still a larger mechanical migration touching many callers. That can now be done independently because the canonical identity, stringification, and hashing paths are already unified.

**Goal**

Remove the split-state representation where `TemplateTypeArg` owns one dependency model and `TemplateInstantiationKey` owns another.

**Primary files**

- `src\TemplateRegistry_Types.h`
- `src\TemplateTypes.h`
- `src\TemplateRegistry_Registry.h`
- parser/template helpers that create `TemplateTypeArg` values

**Concrete work**

1. Introduce a first-class carrier for non-type value identity in `TemplateRegistry_Types.h`.
2. Replace the current `is_value + value + is_dependent + dependent_name` split for value arguments with that carrier.
3. Make `TemplateInstantiationKey` consume the same carrier directly instead of re-deriving `ValueArgKey` from loosely related fields.
4. Move hash/equality/string/mangled-name generation behind shared helpers so all projections come from the same identity model.
5. Audit all `TemplateTypeArg` constructors/factories and all "evaluate expression -> make value arg" sites to ensure they stamp the new identity consistently.
6. Keep bool/int equivalence behavior only where it is intentional for partial-specialization/value matching; document that rule right next to equality/hash code.

**Why this phase is first**

The recent regression proved that name/key generation is the last point where distinct instantiations can still collapse even after parser capture is correct.

**Done when**

- there is one canonical representation of a non-type template argument's dependency identity,
- `TemplateInstantiationKey` no longer needs to reconstruct dependency semantics from ad-hoc fields,
- hash/equality/name generation cannot disagree on whether an argument is concrete or dependent.

**Read/query first**

- `src\TemplateRegistry_Types.h:170-183`
- `src\TemplateRegistry_Types.h:348-365`
- `src\TemplateRegistry_Types.h:483-565`
- `src\TemplateRegistry_Types.h:699-721`
- `src\TemplateTypes.h:185-265`
- `src\TemplateTypes.h:345-360`

---

## Phase 2: centralize alias-template materialization

**Status:** PARTIALLY COMPLETE (targeted fixes landed; authoritative helper still missing)

**Goal**

Make alias-template use resolution one parser-owned service instead of a behavior repeated in top-level using, struct-local using, type-specifier parsing, and substitution helpers.

**Primary files**

- `src\Parser.h`
- `src\Parser_Decl_TopLevel.cpp`
- `src\Parser_Decl_TypedefUsing.cpp`
- `src\Parser_TypeSpecifiers.cpp`
- `src\Parser_Templates_Inst_Substitution.cpp`
- `src\ExpressionSubstitutor.cpp`

**Concrete work**

1. Add one helper with a narrow contract, e.g. "given alias template name + raw/concrete args, return the resolved target type/materialized instantiation result".
2. Move these responsibilities into that helper:
   - alias-parameter substitution
   - concrete non-type argument evaluation
   - alias-chain recursion
   - class-template instantiation
   - late materialized struct registration
   - use-site `TypeSpecifierNode` rewrite
3. Change top-level `using Alias = AliasTemplate<...>;` handling to only gather syntax and call the helper.
4. Change struct-local `using` / typedef alias registration to do the same.
5. Change general type-specifier alias-template handling to route through the same helper instead of its own deferred-substitution loop.
6. Audit `ExpressionSubstitutor.cpp` and any other template-substitution sites that currently instantiate aliases/classes directly so they also reuse the shared helper or an adjacent lower-level primitive.

**Important design constraint**

This helper should return more than just a name string. It should carry enough structured result to avoid each caller redoing type lookup and `TypeSpecifierNode` rebuilding differently.

**Done when**

- parser entry points no longer each contain their own alias-template argument-substitution loop,
- alias-chain behavior is consistent regardless of whether the use site is a type alias, a type specifier, or template substitution,
- fixing alias-template behavior in one place updates all surfaces.

**Read/query first**

- `src\Parser_Decl_TopLevel.cpp:827-1071`
- `src\Parser_Decl_TypedefUsing.cpp:410-434`
- `src\Parser_TypeSpecifiers.cpp:995-1235`
- `src\Parser_Templates_Inst_Substitution.cpp:19-180`

---

## Phase 3: make late materialization + pending-sema normalization explicit

**Status:** NOT STARTED (ready once remaining Phase 2 centralization is narrowed)

**Goal**

Turn the current "register late materialized node, then maybe normalize pending roots if available" pattern into one explicit contract.

**Primary files**

- `src\Parser.h`
- `src\Parser_Core.cpp`
- `src\IrGenerator_Helpers.cpp`
- `src\Parser_Templates_Lazy.cpp`
- `src\Parser_Expr_QualLookup.cpp`
- `src\ConstExprEvaluator_Members.cpp`
- any parser/template instantiation sites that call `registerLateMaterializedTopLevelNode(...)`

**Concrete work**

1. Define the intended lifecycle in code comments and helper naming:
   - materialize AST root
   - register it
   - enqueue it for sema
   - normalize it when a sema owner is active
2. Add one helper that performs the registration/enqueue part together so call sites cannot forget half of the contract.
3. Decide whether normalization belongs:
   - immediately in parser/constexpr call sites when active sema exists, or
   - behind a dedicated queue-drain helper used by parser/constexpr/codegen bridges
4. Replace direct scattered `normalizePendingSemanticRootsIfAvailable()` calls with that one policy.
5. Audit current late-instantiation sites from class-template instantiation, lazy members, qualified lookup, constexpr member lookup, and substitution paths.
6. Ensure the ownership rule is the same regardless of whether the instantiation was triggered by parser lookup, constexpr evaluation, or IR/codegen-side lazy generation.

**Why this matters**

The current architecture already has the pieces for incremental sema normalization. What it lacks is one obvious, enforced path that every new late-instantiation site must use.

**Done when**

- there is one obvious helper or queue contract for late-materialized roots,
- new instantiation sites do not need to remember "register here, normalize there",
- constexpr-triggered and parser-triggered materialization follow the same normalization rule.

**Read/query first**

- `src\Parser.h:976-993`
- `src\Parser_Core.cpp:419-423`
- `src\IrGenerator_Helpers.cpp:5-11`
- `src\ConstExprEvaluator_Members.cpp:1319-1326`
- the `registerLateMaterializedTopLevelNode(...)` call sites across parser/template files

---

## Phase 4: replace unresolved-placeholder heuristics with explicit state

**Status:** NOT STARTED

**Goal**

Stop detecting important dependent placeholder cases by combining incomplete-instantiation state with string-level name inspection.

**Primary files**

- `src\Parser_Templates_Inst_Deduction.cpp`
- `src\TypeInfo*` / template-registry carrier types that currently model placeholder state
- any alias/late-instantiation code that creates dependent placeholder types

**Concrete work**

1. Identify the exact placeholder states that currently surface as "incomplete instantiation with a `::` in the name".
2. Add an explicit kind/flag for unresolved dependent member-alias placeholders or equivalent unresolved-dependent materialization states.
3. Use that explicit state in SFINAE viability checks.
4. Audit whether the same explicit state should also guide alias-template resolution, late instantiation lookup, and constexpr member lookup.
5. Remove or narrow the current string heuristic once the explicit state is available everywhere it is needed.

**Done when**

- SFINAE viability checks do not need to infer unresolved dependent state from names,
- placeholder categories are visible in the type/instantiation model itself,
- future dependent-alias bugs are easier to reason about from debugger/log output.

**Read/query first**

- `src\Parser_Templates_Inst_Deduction.cpp:2175-2190`

---

## Phase 5: only then consider the larger parser/sema ownership move

**Status:** BLOCKED on Phase 2-4 completion and on shrinking the remaining codegen fallback surface

**Goal**

Use the earlier cleanup to make the broader parser/template/sema boundary work practical rather than theoretical.

**Primary references**

- `docs\2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs\2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`

**Concrete work**

1. Revisit whether alias-template instantiation or more late-instantiation logic should move behind a sema-owned incremental work queue.
2. Decide whether template materialization should stay parser-owned with stronger invariants, or whether some subset should become sema-triggered.
3. Keep this as a follow-on after Phases 1-4, not as a prerequisite for them.

**Why this is last**

The recent bug did not require a full architecture rewrite. It exposed specific duplication and split identity first. Removing those is the cheaper and safer way to earn clearer sema ownership later.

---

## Validation matrix

Each phase should at minimum run:

- `.\build_flashcpp.bat Sharded`
- `powershell -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_integral_constant_comprehensive_ret100.cpp test_integral_constant_pattern_ret42.cpp test_ratio_less_alias_ret0.cpp test_sfinae_enable_if_ret0.cpp test_sfinae_same_name_overload_ret0.cpp`

Recommended targeted additions during the refactor:

1. alias chain with dependent bool non-type argument
2. alias-template use through top-level `using`
3. alias-template use through struct-local `using`
4. dependent `enable_if<..., T>::type` return-type viability
5. late-instantiated member/template path that requires pending-sema normalization after materialization

---

## Risks and guardrails

### Risk 1: changing value-arg identity can perturb specialization matching

Guardrail:

- keep bool/int normalization only where the current semantics depend on it,
- do not silently change equality/hash rules without matching tests.

### Risk 2: centralizing alias materialization can accidentally drop use-site qualifiers

Guardrail:

- treat alias target resolution and use-site pointer/reference/cv/array modifiers as separate steps,
- preserve the current merge behavior when rebuilding `TypeSpecifierNode`.

### Risk 3: eager normalization hooks can create re-entrancy surprises

Guardrail:

- make the registration/queue contract explicit before widening normalization calls,
- prefer a queue-drain helper over hidden recursive normalization in many unrelated sites.

### Risk 4: placeholder-state cleanup can overfit one SFINAE bug

Guardrail:

- model unresolved-dependent state broadly enough that it also explains alias-member placeholders and other dependent `::type` shapes,
- do not bake test-specific string patterns into the final representation.

---

## Reviewer-identified issues and refactoring opportunities

The following items were raised during code review (Gemini) and should be addressed as part of or alongside the phases above.

### Dead `else if` in `ExpressionSubstitutor.cpp` (two sites)

At `src/ExpressionSubstitutor.cpp:744` and `src/ExpressionSubstitutor.cpp:1170`, `instantiated_name` is unconditionally assigned `template_name_to_instantiate` (which is non-empty), making the subsequent `else if (instantiated_name.empty())` always false. `get_instantiated_class_name` is therefore dead code. The fix is to try the registry lookup first, then fall back to `get_instantiated_class_name`, and only use the raw template name as a last resort. **Relates to Phase 2.**

### Alias resolution duplication between top-level and struct-local `using`

`src/Parser_Decl_TopLevel.cpp:1019-1054` and `src/Parser_Decl_TypedefUsing.cpp:410-434` both contain the same pattern: check `isTemplateInstantiation`, materialize args via `toTemplateTypeArg`, call `instantiate_and_register_base_template`, and update the `TypeSpecifierNode`. Extract a shared helper (e.g. `resolveAndRegisterAliasTarget`). **Relates to Phase 2.**

### Expression substitution duplication across instantiation sites

`src/Parser_Templates_Inst_ClassTemplate.cpp:441-454` duplicates the `ExpressionSubstitutor` + `ConstExpr::Evaluator::evaluate` dispatch pattern that appears at 9+ other sites in this PR. Extract a shared helper (e.g. `TemplateTypeArg fromEvalResult(const ConstExpr::EvalResult&)` plus a `substituteAndEvaluateNonTypeDefault` wrapper). **Relates to Phase 2.**

### Extract `materialize_placeholder_args` from deduction

The `materialize_placeholder_args` lambda at `src/Parser_Templates_Inst_Deduction.cpp:2059-2178` is ~120 lines. It should be a dedicated `Parser` member function for readability and testability. **Relates to Phase 2 / Phase 3.**

### Extract `normalizeDependentNonTypeArgs` from `parse_type_specifier`

The `normalizeDependentNonTypeArgs` lambda at `src/Parser_TypeSpecifiers.cpp:961-988` is complex and likely needed in other parts of the template parsing pipeline. It should be a dedicated helper method on `Parser`. **Relates to Phase 1.**

### Hash strategy drift between `TemplateTypeArg::hash()` and `ValueArgKey::hash()`

~~`TemplateTypeArg::hash()` (`src/TemplateRegistry_Types.h:267-272`) uses `std::hash<StringHandle>` for `dependent_name`, while `ValueArgKey::hash()` (`src/TemplateTypes.h:196-203`) uses `std::hash<uint32_t>` on the raw `.handle` field. The two types are never compared in the same hash map so this is not a correctness bug today, but it is exactly the kind of drift that Phase 1 canonicalization should eliminate.~~ **ADDRESSED in Phase 1 (2026-04-12):** `ValueArgKey` is now an alias for `NonTypeValueIdentity`, which uses `std::hash<StringHandle>{}(dependent_name)` consistently with `TemplateTypeArg::hash()`.

### Codegen calls back into Parser for template constructor materialization

**Added:** 2026-04-13 (PR #1255 review)

The `materialize_template_ctor` lambda at `src/IrGenerator_Stmt_Decl.cpp:411-422` calls `parser_->materializeMatchingConstructorTemplate(...)` during IR generation, triggering template instantiation from the codegen phase. The semantic analysis path at `src/SemanticAnalysis.cpp:4997-5007` does the same work earlier, but the codegen path retains its own materialization as a fallback when sema did not resolve the constructor (e.g. when no `InitializerListNode` annotation was present, or when the brace-init path fell through to arity-based resolution).

This mirrors the existing `prepare_nested_template_ctor` lambda at `src/IrGenerator_Stmt_Decl.cpp:385-409`, which similarly calls back into `parser_->instantiateLazyMemberFunction(...)` from codegen. Both are instances of the same architectural gap: codegen is performing template instantiation that should have been completed before reaching IR generation.

**Additional issues in the current codegen fallback:**
- The `materialize_template_ctor` lambda captures `is_ambiguous` but never checks it, silently proceeding with `nullptr` instead of throwing an ambiguity error like the sema path does.
- The `is_unresolved_noop_ctor` lambda uses a fragile `type_name.front() == '_'` heuristic to detect unresolved template parameter placeholders, which can false-positive on user-defined types (e.g. `_MyAllocator`), silently skipping the constructor call.
- Both `is_unresolved_noop_ctor` early-return sites (brace-init at line ~1562 and direct-init at line ~2366) skip destructor registration, causing resource leaks when the struct has a destructor.

**Partial fix (2026-04-14, proper sema fix):** The primary root cause — lazy template constructor bodies not having struct member scope during re-parse — is now fixed in `Parser_Templates_Lazy.cpp`. The `is_unresolved_noop_ctor` codegen fallback is no longer triggered for the common template-ctor case. The codegen fallback paths remain as defensive code; Phase 5 should eliminate them entirely by ensuring sema normalizes all function bodies.

**Long-term fix:** Resolve all constructor templates in semantic analysis before reaching codegen. The sema path already partially does this; the codegen fallback should become unnecessary once sema covers all constructor-call shapes (including arity-based fallback and direct-init syntax). **Relates to Phase 3 and Phase 5.**

### `appendFunctionCallArgType` lacks full type deduction for complex expressions

**Added:** 2026-04-13 (PR #1255 review)

`Parser::appendFunctionCallArgType()` at `src/Parser_Core.cpp:346-378` produces best-effort `TypeSpecifierNode` entries for function-call overload resolution and template argument deduction. It handles `BoolLiteralNode`, `NumericLiteralNode`, `StringLiteralNode`, and `IdentifierNode` (which correctly copies the full `TypeSpecifierNode` including `type_index` from the declaration). However, all other expression types (`CallExprNode`, `BinaryOperatorNode`, `MemberAccessNode`, casts, etc.) fall through to the `TypeCategory::Int` default at line 352.

This is not a regression — the old inline code it replaced (deleted lines ~4456-4481 in `src/Parser_Expr_PrimaryExpr.cpp`) had the same limitation, only handling the same four expression types and defaulting to `Int` for everything else. The function is currently only used in the template-function-call path where `arg_types` feed `buildDeductionMapFromCallArgs` for template argument deduction rather than full overload resolution, so the impact is limited.

~~**Follow-up:** Extend `appendFunctionCallArgType` to handle compound expressions by computing their result type (e.g., propagating the return type of a `CallExprNode`, the result type of a `BinaryOperatorNode`, etc.). This would improve deduction accuracy for calls like `foo(bar() + 1)` where the argument type should be deduced from the expression's result rather than defaulting to `int`. **Relates to Phase 6.**~~

**FIXED (2026-04-13):** `appendFunctionCallArgType` now handles `StaticCastNode`, `ConstCastNode`, `ReinterpretCastNode`, `DynamicCastNode` (extract target type), `CallExprNode` with a resolved callee (use declared return type), and `UnaryOperatorNode` (propagate operand type for arithmetic/bitwise; deduce Bool for `!`; fall through to Invalid for `*`/`&`). `BinaryOperatorNode`, `MemberAccessNode`, and other complex expression types still default to `TypeCategory::Int` and can be addressed in future passes.

### Brace-init codegen path guards `materialize_template_ctor` behind `resolution.has_match`

**Added:** 2026-04-13 (PR #1255 review — active bug)

**FIXED (2026-04-14 slice):** `materialize_template_ctor` is now called unconditionally in the brace-init codegen path, mirroring the direct-init pattern.

### Sema `tryAnnotateInitListConstructorArgs` missing `materializeMatchingConstructorTemplate` call

**Added:** 2026-04-13 (PR #1255 review — active bug)

**FIXED (2026-04-14 slice):** `SemanticAnalysis::tryAnnotateInitListConstructorArgs` now calls `materializeMatchingConstructorTemplate` after `resolve_constructor_overload`, matching the direct-init sema path.

### `is_unresolved_noop_ctor` early return leaves object uninitialized before destructor registration

**Added:** 2026-04-13 (PR #1255 review — investigate)

~~The `is_unresolved_noop_ctor` early-return paths at `src/IrGenerator_Stmt_Decl.cpp` (brace-init ~line 1595 and direct-init ~line 2400) skip the `ConstructorCallOp` emission that normally handles stack allocation for struct variables, but still call `register_destructor_if_needed`. This means the variable is never allocated on the stack, yet a destructor call is registered. Currently this is safe because the two regression tests (`test_template_ctor_noop_dtor_direct_init_ret42.cpp` and `test_template_ctor_noop_dtor_list_init_ret42.cpp`) use destructors that only modify a global variable and never dereference `this`. For types whose destructors access member data, this would be undefined behavior.

Note: `VariableDeclOp` IS emitted at line 1461 (brace-init) and line 2015 (direct-init), both before the `is_unresolved_noop_ctor` check. So the stack space is allocated. The risk is that member data is uninitialized (not zero-initialized) when the destructor runs, which is UB for destructors that access members.

Follow-up: The noop path should zero-initialize the allocated stack object before registering the destructor, or the long-term fix is to resolve all template constructors in sema so the noop path is never reached.~~

**FIXED (2026-04-14, audit update):** both noop-ctor early-return paths now zero-fill the allocated object (including base subobjects via recursive member stores) before registering the destructor.

**SUPERSEDED (2026-04-14, proper sema fix):** The root cause — missing struct member scope during lazy template constructor body re-parse — is now fixed in `Parser_Templates_Lazy.cpp`. Template constructor bodies are properly parsed with `FunctionParsingScopeGuard`, so `is_unresolved_noop_ctor` is no longer triggered for the common case. The zero-fill safety net and the noop fallback remain as defensive code for edge cases but should be removed in Phase 5 when all codegen fallback materialization is eliminated.

### `materializeMatchingConstructorTemplate` returns original uninstantiated ctor on failure

**Added:** 2026-04-13 (PR #1255 review — investigate)

~~In `src/Parser_Templates_Inst_MemberFunc.cpp:221-235`, when `preferred_ctor` has template parameters but `try_instantiate_constructor_template` fails or the instantiated ctor doesn't match call arguments, the function returns `preferred_ctor` (the original uninstantiated template). Downstream code may then use an uninstantiated template constructor for codegen. For empty-body constructors this is handled by `is_unresolved_noop_ctor`, but for non-empty-body constructors that fail instantiation, the returned uninstantiated ctor could cause issues. The function silently returns an unusable ctor instead of signaling failure.~~

**FIXED (2026-04-13):** `materializeMatchingConstructorTemplate` now returns `nullptr` (not the uninstantiated template) when `try_instantiate_constructor_template` fails or the instantiated ctor doesn't match call arguments. Callers fall back to arity-based resolution. **Test:** `tests/test_template_ctor_nullptr_ret0.cpp`.

### `try_instantiate_constructor_template` creates persistent dummy AST nodes

**Added:** 2026-04-13 (PR #1255 review — investigate)

~~`try_instantiate_constructor_template` at `src/Parser_Templates_Inst_MemberFunc.cpp:103-194` creates dummy `TypeSpecifierNode`, `DeclarationNode`, and `FunctionDeclarationNode` nodes solely to call `buildDeductionMapFromCallArgs`. These nodes are allocated via `emplace_node` and persist in the AST node pool for the lifetime of compilation. If `materializeMatchingConstructorTemplate` iterates many template constructors (~line 238-262), this could create significant node churn.~~

**FIXED (2026-04-13, audit update):** `buildDeductionMapFromCallArgs(...)` now has an overload that accepts function parameter nodes directly, and `try_instantiate_constructor_template(...)` reuses `ctor_decl.parameter_nodes()` instead of allocating persistent dummy wrapper nodes. **Relates to Phase 6.**

### `try_instantiate_member_function_template` fallback to `arg_types[0]` is suspicious

**Added:** 2026-04-13 (PR #1255 review — pre-existing)

**FIXED (2026-04-14 slice):** `try_instantiate_member_function_template(...)` no longer falls back to `arg_types[0]`. It now uses a default template argument when present and otherwise fails deduction. See `tests/test_member_function_template_undeduced_param_fail.cpp`.

### Constructor call pack expansion duplicates logic and misses identifier-pack path

**Added:** 2026-04-13 (PR #1255 review)

~~The constructor argument parsing path at `src/Parser_Expr_PrimaryExpr.cpp` (the "Parse constructor arguments with pack expansion support" section, around line 3561) handles `arg...` pack expansion by calling `expandPackExpressionArgument(*node)` directly. This only covers `pack_param_info_`-based expansion (complex expression packs like `static_cast<T>(args)...`). The function call argument path uses the `append_function_call_argument` lambda (around line 4413) which handles both simple identifier packs and complex expression packs. The constructor path is missing strategy (1).~~

**FIXED (2026-04-13):** Both the constructor expression path (`src/Parser_Expr_PrimaryExpr.cpp:3565`) and the declaration path (`src/Parser_Statements.cpp` `parse_direct_initialization`) now implement both expansion strategies (simple identifier packs via `get_pack_size` + complex packs via `expandPackExpressionArgument`), matching `append_function_call_argument` exactly. **Test:** `tests/test_ctor_direct_init_pack_ret0.cpp`.

---

## Phase 6: unify template-parameter-to-function-parameter deduction mapping

**Date added:** 2026-04-11
**Context:** PR #1207 exposed that `try_instantiate_template_explicit` uses a positional counter (`deduced_call_arg_index`) to map template parameters to call arguments. This is architecturally incorrect — it assumes template parameter `i` corresponds to function parameter `i`, which breaks for SFINAE guards, non-deducible params, and reordered mappings.

### Current state

There are **two separate deduction paths** with different mapping strategies:

1. **`try_instantiate_template_explicit`** (`src/Parser_Templates_Inst_Deduction.cpp:446-1159`)
   — the "explicit template args + call args" path. Uses a blind positional counter
   `deduced_call_arg_index` that walks `current_explicit_call_arg_types_` in order.
   No mapping between template parameter names and function parameter types.

2. **`try_instantiate_single_template`** (`src/Parser_Templates_Inst_Deduction.cpp:1282-1670`)
   — the "deduce everything from call args" path. Has a **pre-deduction pass**
   (lines 1361-1522) that builds a `param_name_to_arg` map by inspecting function
   parameter types against template parameter names. This is architecturally correct.

### The problem with `deduced_call_arg_index`

For `template<typename T, typename = decltype(swap(...))> true_type test(int)` called as
`test<pair<const int,int>>(0)`:

- Template param 0 (`T`): consumed from explicit args ✓
- Template param 1 (SFINAE guard): `deduced_call_arg_index=0` still points to the `int`
  call arg → **incorrectly consumes it** instead of using the default

The current workaround (PR #1207) gates call-arg deduction on `!param.has_default()`.
This works for SFINAE guards (which always have defaults) but is not C++20-correct for
`template<typename T, typename U = int> void foo(T, U)` called as `foo<double>(1.0, "hello")`
— `U` should be deduced as `const char*` from the 2nd call arg, but the workaround uses
the default `int` because `U` has a default.

### Recommended architectural fix

Add a **pre-deduction pass** to `try_instantiate_template_explicit`, analogous to the one
in `try_instantiate_single_template`. Specifically:

1. **Build a `param_name_to_arg` map** before the main template-arg-building loop.
   Walk the function parameter list (`func_decl.parameter_nodes()`), check each
   parameter's type against the template parameter name set, and match against
   `current_explicit_call_arg_types_` by position in the *function* parameter list
   (not the template parameter list).

2. **In the main loop**, when a template parameter isn't covered by explicit args:
   - Check `param_name_to_arg` first (name-based deduction from function params)
   - Then fall back to `appendDefaultTemplateArg` (defaults)
   - Then fail with overload mismatch

3. **Remove `deduced_call_arg_index`** entirely — it is the root cause of the
   positional mapping bug.

### Why this is correct

The pre-deduction approach works because it maps template parameters to function
parameters **by name** rather than by position:

- For `template<typename T, typename U> void foo(T a, U b)` called as `foo<double>(1.0, "hello")`:
  function param 0 has type `T` → maps to template param `T` (already explicit)
  function param 1 has type `U` → maps to call arg 1 (`"hello"`) → deduces `const char*`

- For `template<typename T, typename = decltype(...)> true_type test(int)`:
  function param 0 has type `int` (concrete) → no template param mapping
  template param 1 (SFINAE guard) has no function param → falls through to default

### Shared helper opportunity

The pre-deduction logic in `try_instantiate_single_template` (lines 1361-1522) and the
proposed new logic for `try_instantiate_template_explicit` are structurally identical.
They should be extracted into a shared helper, e.g.:

```cpp
// Build a map from template parameter names to deduced TemplateTypeArgs
// by matching function parameter types against call argument types.
std::unordered_map<StringHandle, TemplateTypeArg>
Parser::buildDeductionMapFromCallArgs(
    const std::vector<ASTNode>& template_params,
    const FunctionDeclarationNode& func_decl,
    const std::vector<TypeSpecifierNode>& call_arg_types);
```

This would eliminate ~160 lines of duplication and ensure both paths use the same
deduction logic.

### Files to change

- `src/Parser_Templates_Inst_Deduction.cpp` — add pre-deduction pass to
  `try_instantiate_template_explicit`, extract shared helper from
  `try_instantiate_single_template`
- `src/Parser.h` — declare the shared helper

### Validation

- `test_namespaced_pair_swap_sfinae_ret0.cpp` — SFINAE guard with `decltype` default
- `test_pack_decltype_simple_ret42.cpp` — pack expansion in `decltype` base class
- New test needed: `template<typename T, typename U = int> void foo(T, U)` called as
  `foo<double>(1.0, "hello")` to verify `U` is deduced as `const char*` (not `int`)
- Full test suite regression check

---

## Out of scope for this document

- a full rewrite that moves all template instantiation out of the parser immediately
- a full constexpr architecture rewrite
- unrelated codegen/sema ownership cleanup outside the template-materialization surface

---

## Related docs

- `docs\2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs\2026-04-06-constructor-overload-resolution-refactor.md`
- `docs\2026-04-04-codegen-name-lookup-investigation.md`
- `docs\2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`

---

## Bottom line

The branch fix solved the immediate regression by teaching instantiation keys to distinguish dependent and concrete non-type arguments. The next worthwhile architectural step is to make that distinction authoritative everywhere, then collapse alias-template materialization and late-materialization normalization onto explicit shared helpers.

That is the highest-leverage follow-up because it attacks the actual fault line that produced the recent failures: duplicated template identity logic plus duplicated materialization paths.
