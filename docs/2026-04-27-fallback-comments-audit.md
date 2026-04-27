# 2026-04-27 Fallback Comments Audit

## Scope

This audit reviewed C++ source comments containing `fallback` in `src\*.h` and `src\*.cpp`. The focus is not the spelling of variables such as `fallback_size_bits_` by itself, but comment-backed control flow where the compiler intentionally recovers from missing information. Most of these are not C++20 semantic fallbacks; they are symptoms of parser, semantic analysis, template instantiation, or IR/codegen phase boundaries being incomplete.

## Executive summary

The risky fallback sites cluster into five architectural gaps:

1. Codegen still performs semantic decisions when `SemanticAnalysis` has not annotated expression types, conversions, overload targets, or callable targets.
2. Template instantiation still relies on secondary substitution, name scans, placeholder types, and TypeIndex recovery instead of preserving canonical dependent-type bindings.
3. Lazy member materialization is partly enforced before codegen, but residual comments show several places where codegen historically compensated for missing sema materialization.
4. Type size and type identity are split between authoritative metadata and parser-stored or alias-stored fallback fields.
5. Parser recovery skips unsupported template/friend/member constructs, which can turn unsupported C++ into silently accepted incomplete ASTs.

The removal strategy should be to make the higher phase produce complete metadata and make lower phases fail with `CompileError` or `InternalError` when required metadata is absent. In particular, codegen should not be doing overload resolution, type inference, template lookup, or substitution recovery.

## High-risk findings

### 1. Codegen-side call and overload recovery

Representative sites:

- `src\IrGenerator_Call_Direct.cpp:613` - arity-based `operator()` lookup for call sites not reached by sema.
- `src\IrGenerator_Call_Direct.cpp:1196`, `src\IrGenerator_Call_Direct.cpp:1207`, `src\IrGenerator_Call_Direct.cpp:1217`, `src\IrGenerator_Call_Direct.cpp:1238` - pointer, symbol-table, precomputed-mangle, and current-struct fallbacks to recover direct call targets.
- `src\IrGenerator_Call_Indirect.cpp:275` - parser fallback for inconclusive callable type.
- `src\IrGenerator_MemberAccess.cpp:2919` and `src\IrGenerator_MemberAccess.cpp:3047` - type traits choose constructors or assignment operators by arity or first non-implicit operator when exact matching fails.
- `src\OverloadResolution.h:1130` - arity-only constructor overload resolution when argument type information is unavailable.

Missing feature:

`SemanticAnalysis` and overload resolution do not yet guarantee that every call expression, constructor expression, callable object invocation, and builtin type-trait query carries the selected declaration and exact conversion sequence before IR generation.

Why the fallback is non-compliant:

C++ overload resolution cannot degrade to arity-only, first-match, unique-name, or symbol-table scan semantics. These heuristics can select the wrong overload, ignore constraints, mishandle cv/ref qualifiers, and produce code for an ill-formed program.

Removal direction:

- Treat resolved call target identity as mandatory sema output for all normalized bodies, including template-instantiated bodies and lazy member bodies.
- Store the selected constructor/operator/function declaration on the AST or in a stable sema side table keyed by the call node.
- Make codegen consume that target only. If a normalized body reaches codegen without it, throw `InternalError`.
- Keep only true diagnostic/error-recovery paths in parser, not codegen selection heuristics.

### 2. Codegen-side type conversion and expression typing

Representative sites:

- `src\IrGenerator_Expr_Operators.cpp:738`, `src\IrGenerator_Expr_Operators.cpp:750`, `src\IrGenerator_Expr_Operators.cpp:758` - ternary lowering falls back from sema annotations to sema expression types and then parser expression typing.
- `src\IrGenerator_Expr_Operators.cpp:2840` through `src\IrGenerator_Expr_Operators.cpp:2875` - binary operators prefer sema conversions but still generate fallback standard arithmetic conversions.
- `src\IrGenerator_Expr_Conversions.cpp:1484`, `src\IrGenerator_Expr_Conversions.cpp:1506`, `src\IrGenerator_Expr_Conversions.cpp:2535` - conversion lowering falls back to promotion or direct user-defined conversion search when sema is missing or unavailable.
- `src\IrGenerator_Visitors_Namespace.cpp:360` - namespace visitor checks conversion operators when sema did not run.
- `src\IrGenerator_Stmt_Decl.cpp:1975` - variable initialization searches conversion operators directly when sema annotation is absent.

Missing feature:

The sema-to-codegen contract for expression type and conversion annotations is incomplete. Some normalized bodies still reach codegen without explicit conversion edges and without a canonical expression type for every subexpression.

Why the fallback is non-compliant:

Usual arithmetic conversions, contextual conversions, user-defined conversions, ternary common type calculation, and initialization conversions are semantic operations. Performing them opportunistically in codegen risks applying conversions in the wrong context, missing narrowing diagnostics, bypassing overload resolution ranking, and producing implementation-specific behavior.

Removal direction:

- Require sema to annotate every implicit conversion, including ternary branch conversions, assignment RHS conversions, binary arithmetic conversions, contextual `bool`, and user-defined conversions.
- Extend sema coverage to template-instantiated, catch-block, lambda, and delayed member bodies so parser expression typing is never needed by codegen.
- Replace codegen fallbacks with assertions once all normalized body contexts are covered.
- Keep codegen conversion helpers only as emitters for an already-selected conversion, not as selectors.

### 3. Template instantiation and substitution recovery

Representative sites:

- `src\ExpressionSubstitutor.cpp:951` and `src\Parser_Templates_Lazy.cpp:1012` - general fallback substitution for remaining template-dependent expressions.
- `src\ExpressionSubstitutor.cpp:1537` - recovers template arguments from type names when TypeInfo has no stored args.
- `src\Parser_Templates_Inst_ClassTemplate.cpp:4049`, `src\Parser_Templates_Inst_ClassTemplate.cpp:4589`, `src\Parser_Templates_Inst_ClassTemplate.cpp:4787`, `src\Parser_Templates_Inst_ClassTemplate.cpp:4813`, `src\Parser_Templates_Inst_ClassTemplate.cpp:5246`, `src\Parser_Templates_Inst_ClassTemplate.cpp:5844`, `src\Parser_Templates_Inst_ClassTemplate.cpp:5904` - non-type defaults, variable templates, array dimensions, initializers, and static members use catch-all substitution or AST fallback passes.
- `src\Parser_Templates_Substitution.cpp:692`, `src\Parser_Templates_Substitution.cpp:762`, `src\Parser_Templates_Substitution.cpp:848`, `src\Parser_Templates_Substitution.cpp:1633` - pack and template parameter lookup fallbacks after primary scope information is unavailable.
- `src\Parser_Templates_Params.cpp:1826` - `type_index == 0` is treated as a fallback indicator for dependent types.

Missing feature:

Template instantiation lacks one authoritative dependent-type and dependent-expression substitution pipeline. Template parameter bindings, pack bindings, non-type parameter values, array bounds, nested types, and instantiated TypeInfo metadata can be lost and later reconstructed from names or parser state.

Why the fallback is non-compliant:

C++ template semantics depend on precise dependent identity, current instantiation rules, two-phase lookup, pack expansion rules, non-type template parameter value/category, and constraints. Name-based or positional fallback can bind the wrong declaration, lose cv/ref or value category, accept ill-formed substitutions, or fail to diagnose SFINAE vs hard-error boundaries correctly.

Removal direction:

- Introduce a single substitution context object that carries type parameters, non-type values, packs, current instantiation, and namespace/member context through all template instantiation paths.
- Store canonical template arguments and pattern-to-instantiation links in TypeInfo/TemplateRegistry at creation time rather than reconstructing from strings.
- Replace `type_index == 0` dependent detection with explicit dependent placeholder metadata.
- Make ExpressionSubstitutor a normal part of substitution with explicit preconditions, not a catch-all fallback after parser/template code failed to substitute.

### 4. Lazy member materialization fallback history

Representative sites:

- `src\FlashCppMain.cpp:602` - deferred member generation is described as coming from a struct search fallback in call lowering.
- `src\IrGenerator_Call_Direct.cpp:1074`, `src\IrGenerator_Call_Direct.cpp:1390`, `src\IrGenerator_Call_Direct.cpp:1692` - comments document historical or retained lazy member materialization fallbacks around direct calls.
- `src\IrGenerator_Call_Indirect.cpp:1081` - historical indirect-call lazy member fallback.
- `src\SemanticAnalysis.cpp:3812`, `src\SemanticAnalysis.cpp:5257`, `src\SemanticAnalysis.cpp:5458` - sema comments identify lazy materialization as the replacement for codegen-side fallbacks.

Missing feature:

The architecture is partway through moving lazy member materialization to semantic analysis, but comments and remaining recovery paths show the invariant is not fully encoded: all ODR-used and reachable instantiated members must exist before codegen.

Why the fallback is non-compliant:

On-demand body materialization from codegen couples declaration lookup, overload selection, template instantiation, and emission order. It can miss ODR-use cases, instantiate members under the wrong context, or silently select pattern-owned declarations instead of instantiated declarations.

Removal direction:

- Make sema's end-of-normalization drain the only lazy materialization path.
- Record ODR-use and selected instantiated member declarations during sema call resolution.
- Add hard checks before codegen begins: no lazy selected call target may still point to an unmaterialized stub when its body is required.
- Delete dead historical comments after replacing them with executable assertions or tests.

### 5. Parser skip fallbacks for unsupported syntax

Representative sites:

- `src\Parser_Templates_Class.cpp:246` and `src\Parser_Templates_Class.cpp:559` - nested template parsing skips the rest when parsing fails or the pattern is not recognized.
- `src\Parser_Templates_Class.cpp:4459` - specialization without template arguments uses a should-not-happen fallback.
- `src\Parser_Decl_StructEnum.cpp:4339` - qualified friends or unparsed params skip bodies.
- `src\Parser_FunctionHeaders.cpp:126` - falls back to normal expression parsing after failing another interpretation.

Missing feature:

The parser does not fully model all member template, out-of-line definition, friend, requires/trailing-specifier, and ambiguous declaration/expression grammar forms it encounters in C++20/library headers.

Why the fallback is non-compliant:

Skipping tokens to keep parsing can convert unsupported or ill-formed constructs into accepted but incomplete declarations. Later phases then compensate for missing declarations, bodies, template parameters, or constraints.

Removal direction:

- Split valid grammar ambiguity handling from unsupported-feature recovery.
- For valid C++20 constructs, parse into explicit AST nodes with enough metadata for sema.
- For unsupported constructs, emit a targeted `CompileError` unless the file is being parsed in a deliberately configured system-header recovery mode.
- Add tests for each skipped construct before replacing skip fallbacks.

## Medium-risk findings

### 6. Type size and type identity fallback fields

Representative sites:

- `src\AstNodeTypes_DeclNodes.h:938` - `TypeInfo::fallback_size_bits_` stores generic size for aliases, incomplete types, and forward declarations.
- `src\AstNodeTypes_DeclNodes.h:1831` through `src\AstNodeTypes_DeclNodes.h:1878` - `getTypeSpecSizeBits` uses StructTypeInfo first, TypeInfo stored size next, scalar size next, then parser-stored `type_spec.size_in_bits()`.
- `src\AstNodeTypes.cpp:443` - enum sizing fallback when concrete enum TypeIndex has been lost.
- `src\AstNodeTypes.cpp:645` - fallback names for non-primitive types.
- `src\IrGenerator_MemberAccess.cpp:1930` and `src\IrGenerator_MemberAccess.cpp:1935` - `sizeof` member handling falls back to TypeInfo and TypeSpecifier size metadata.
- `src\TemplateRegistry_Lazy.h:1565`, `src\TemplateRegistry_Lazy.h:1568`, `src\TemplateRegistry_Lazy.h:1581` - lazy registry converts fallback size bits or primitive type sizes.

Missing feature:

The compiler does not have a single authoritative type layout query for all complete object types and aliases. Size can come from StructTypeInfo, EnumTypeInfo, TypeInfo fallback fields, TypeSpecifier parser snapshots, native category tables, or alias metadata.

Why the fallback is risky:

For aliases, dependent instantiations, forward declarations, and enums, stale or parser-cached sizes can hide incomplete type use or return the wrong size after substitution.

Removal direction:

- Centralize size/layout queries behind a `TypeLayout` service that takes canonical TypeIndex plus qualifiers/pointer/reference state.
- Keep parser-stored size only as a display/cache field for primitive tokens, not as a fallback authority.
- Replace `fallback_size_bits_` with explicit states: complete layout, known scalar alias, dependent/incomplete, and invalid.
- Make `sizeof` and array-bound code reject incomplete object types rather than falling through to zero or cached parser size.

### 7. `sizeof` and constexpr evaluation fallbacks

Representative sites:

- `src\IrGenerator_MemberAccess.cpp:1996` - `sizeof(member_access)` searches instantiated types by string prefix when direct lookup finds unsubstituted template members.
- `src\IrGenerator_MemberAccess.cpp:2081`, `src\IrGenerator_MemberAccess.cpp:2110`, `src\IrGenerator_MemberAccess.cpp:2131`, `src\IrGenerator_MemberAccess.cpp:2151` - `sizeof(arr[index])` and general `sizeof(expr)` fall through to IR generation and can return zero with only a warning.
- `src\ConstExprEvaluator_Core.cpp:4002` - `__is_complete_or_unbounded` returns true if it cannot extract the type.
- `src\ConstExprEvaluator_Members.cpp:4194` - struct initializer evaluation falls back to evaluating the initializer expression directly.
- `src\IrGenerator_Visitors_TypeInit.cpp:1175`, `src\IrGenerator_Visitors_TypeInit.cpp:1420` - type initialization tries full constexpr eval or emits bytes from fallback zero-initialization.

Missing feature:

Compile-time evaluation and type trait evaluation are not consistently driven by semantic type metadata. When type extraction or member layout fails, code falls back to runtime IR generation, direct evaluation, or permissive trait answers.

Why the fallback is non-compliant:

`sizeof` is never runtime-evaluated in C++; it requires a complete compile-time type except for permitted cases. Traits such as completeness checks must not return success because type extraction failed. Returning true or zero-initializing can make invalid programs compile.

Removal direction:

- Add sema-owned type resolution for all `sizeof`, `alignof`, trait, and initializer contexts.
- Make constexpr/type-trait evaluators distinguish "false by rule" from "unknown due compiler limitation".
- Replace permissive fallback answers with `CompileError` for user-facing invalid code or `InternalError` for missing compiler metadata.
- Ensure instantiated member access preserves the instantiated TypeIndex so `sizeof(t.member)` never searches by string prefix.

## Lower-risk or acceptable fallback comments

Some comments use "fallback" for non-semantic recovery or platform integration:

- `src\CrashHandler.h:362` installs an unhandled exception filter fallback.
- `src\FileReader_Macros.cpp:1190` handles `#include_next` by using normal include lookup when the current directory is not in include paths.
- `src\Token.h:71` returns a fallback token spelling for tokens not in a table.
- `src\CodeViewDebug.cpp:771` uses a fallback object filename for debug info.
- `src\ObjFileWriter.h:316` reports failure after both COFFI and manual object writer paths fail.
- `src\IRConverter_ABI.h:271` and `src\IRConverter_ABI.h:295` remember dirty registers as allocator fallbacks.

These are not C++ semantic fallbacks. They may still deserve cleanup, but they do not indicate standard-compliance gaps in the compiler front-end.

## Recommended removal order

1. **Sema/codegen contract hardening**: make call targets, constructor targets, expression types, and implicit conversions mandatory for normalized bodies. Convert codegen fallbacks to assertions after adding missing sema coverage.
2. **Template substitution unification**: carry explicit substitution context and dependent placeholder metadata through all instantiation paths; remove name-based and `type_index == 0` recovery.
3. **Lazy member pre-codegen invariant**: enforce that ODR-used instantiated member bodies are materialized before IR generation starts.
4. **Authoritative type layout service**: replace `fallback_size_bits_` and parser-size fallback reads with explicit complete/incomplete/dependent layout states.
5. **Parser recovery split**: parse supported C++20 constructs fully; for unsupported constructs, produce targeted errors instead of skipping and letting later phases recover.

## Practical next audit steps

- Add temporary hard-fail guards behind a debug flag for codegen fallbacks in `IrGenerator_Call_Direct.cpp`, `IrGenerator_Expr_Operators.cpp`, `IrGenerator_Expr_Conversions.cpp`, and `IrGenerator_MemberAccess.cpp` to measure which sites still execute in the current corpus.
- For each executing site, add a regression test that demonstrates the sema/template metadata gap before removing the fallback.
- Prefer deleting comments for already-removed historical fallbacks once their invariant is enforced by a test or assertion.

## Probe results from 2026-04-27 validation

The initial audit above was architectural. Several template-instantiation fallbacks were then probed directly by replacing them with hard failures or by deleting the fallback path and running the full Windows validation (`.\build_flashcpp.bat` and `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`). The audit document keeps only the still-relevant post-audit state below rather than a historical list of removed fallbacks.

### Proven active in the current corpus

1. `src\Parser_Templates_Inst_ClassTemplate.cpp` — unresolved template-default catch-all
   - Previous behavior: if no handler resolved a default template argument, the code pushed a placeholder (`void` for type params, `0` for non-type params).
   - Probe result: replacing it with a hard error broke `tests\test_template_template_default_ret42.cpp`.
   - Conclusion: this fallback is still active and cannot be removed safely until the default-argument substitution gap is fixed at the root.

2. `src\Parser_Templates_Inst_ClassTemplate.cpp` — unresolved class-template type argument falls back to using the original `TypeSpecifierNode` as-is
   - Probe result: replacing this with a hard error broke deferred-base and pack-expansion cases including `tests\test_nttp_base_class_substitution_ret0.cpp`, `tests\test_pack_expansion_base_class_ret0.cpp`, `tests\test_ratio_negative_lazy_member_ret0.cpp`, and `tests\test_type_traits_dependent_member_nttp_ret42.cpp`.
   - Conclusion: this fallback is active and currently carries real class-template substitution traffic, especially around deferred base resolution and dependent member/type-trait cases.

3. `src\Parser_Templates_Params.cpp` — `type_index == 0` dependent-type fallback
   - Probe result: replacing the dependent-marking path with a hard error broke comparison/operator template cases including `comparison_operators_ret1.cpp`, `float_comparisons_ret1.cpp`, `test_const_member_with_param_ret255.cpp`, `test_decltype_function_template_base_ret42.cpp`, and multiple spaceship tests.
   - Conclusion: invalid/placeholder `TypeIndex` is still an active dependency signal in the current parser/template pipeline and cannot be removed before the placeholder metadata path is finished.

### Confidence update

The audit is now backed by direct suite evidence for several representative template fallbacks:

- some narrow substitution recoveries were already dead in the current corpus and have now been removed;
- the function-shaped and constructor-shaped deferred-member queue bridges in `IrGenerator_Visitors_TypeInit.cpp` were also dead in the current corpus and have now been replaced with hard invariants;
- multiple class-template/dependent-type fallback paths are definitely active;
- the larger ExpressionSubstitutor/static-initializer/pack-size/dependent-placeholder fallback classes should still be assumed active until probed or root-fixed individually.
