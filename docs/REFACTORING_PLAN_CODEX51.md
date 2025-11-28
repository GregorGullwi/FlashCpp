# Refactoring Plan: Unifying Function Parsing & Codegen

## Background
The current front-end handles free functions, member functions, template functions, and template member functions via partially duplicated code paths. Each path evolved to plug specific gaps (e.g., scope setup, template parameter binding, implicit `this` injection), which makes subtle fixes or feature work require touching three or four different implementations. This duplication increases the risk of behavioral skew (e.g., template member parsing skipping attributes that non-template members support) and inflates maintenance costs.

## Objectives
- **Single source of truth** for the mechanics of parsing, semantic analysis, instantiation, and IR emission of functions regardless of their enclosing context.
- **Declarative function metadata** that captures the differences between free/member/template specializations without branching everywhere.
- **Composable pipelines** that let future features (coroutines, concepts checks, new attributes) plug into a shared workflow.
- **Incremental adoption** so existing behavior stays stable while duplicated paths converge.

## Proposed Architecture
### 1. FunctionShape descriptor
Introduce a light-weight `FunctionShape` (name TBD) that describes everything unique about the function being parsed/instantiated:
- `FunctionKind kind` (`Free`, `Member`, `Lambda`, `Ctor`, `Dtor`, ...).
- `bool isTemplate` + pointer to template parameter list / specialization info.
- `ScopeHandle declarationScope` (namespace/class/instantiation context).
- `ImplicitParams implicitParams` (e.g., `this`, injected template params).
- `RequiresClause`, attribute packs, default arguments, etc.

All parsers build this descriptor first, then hand it to a unified builder.

### 2. Layered pipeline
Refactor the existing code into four layers that all consume `FunctionShape`:
1. **Declaration Parser**: token-level parsing that populates AST nodes but defers context-specific tweaks to helpers that consult the shape (e.g., automatically prepend implicit `this`).
2. **Semantic Binder**: resolves names, performs overload resolution, wires template arguments, but shares logic between template/non-template via policy objects (e.g., `TemplateBindingPolicy` with `NonTemplatePolicy` and `PrimaryTemplatePolicy`).
3. **Instantiation/Cloner**: for templates, clones the canonical AST with substitution data. Non-templates use the same interface but a no-op policy, ensuring code reuse.
4. **Lowering Pipeline**: IR emission should receive a `FunctionContext` built from the shape, so member vs free functions differ only by parameter layout and calling convention bits.

### 3. Context objects instead of branching
Replace ad-hoc flags (e.g., `isMember`, `inTemplate`) sprinkled across functions with context structs passed down the stack:
```cpp
struct FunctionContext {
	FunctionShape shape;
	ScopeStack &scope;
	TemplateEnvironment *templEnv;
	IRBuilder &ir;
};
```
Helpers such as `injectImplicitParams(FunctionContext&)` become reusable utilities, eliminating duplicated preambles in the various parsing entry points.

### 4. Shared template plumbing
- Extract template parameter collection, constraint checking, and specialization lookup into a `TemplateFunctionCoordinator` used by both free and member templates.
- Maintain a single `TemplateBodyCache` keyed by the canonical `FunctionShape`, so instantiation logic runs uniformly.

### 5. Declarative transformations
Where behavior currently diverges via hand-rolled branches, introduce small tables/policies. Examples:
- Access specifier rules for special member functions.
- Attribute propagation (e.g., `constexpr`, `noexcept`) handled by a rule map keyed by `FunctionKind`.
- Storage duration & linkage derived from the enclosing scope type, not manual conditionals per parser.

## Scope & Impact Mapping
| Subsystem | Current duplication pain | Expected change |
|-----------|-------------------------|-----------------|
| Parser entry points (`parseFunction*`) | Four nearly-identical preambles for scope+attributes | Collapse into one dispatcher that just builds `FunctionShape` |
| Semantic analyzer (`bindFunction*`, implicit parameter wiring) | Divergent handling for `this`, constraints, access | Replace with `FunctionContext`-aware helpers |
| Template instantiation cache | Separate maps for free/member templates | Single `TemplateBodyCache` keyed by canonical shape |
| IR emission (`emitFunction`, `emitTemplateFunction`) | Parameter order and calling convention duplicated | Shared lowering pipeline fed by `FunctionContext` |

## Migration Strategy (Phased)
### Phase 0 — Discovery & Guardrails (1 week)
1. **Audit** current entry points (`parseFunction`, `parseMemberFunction`, `parseTemplateFunction`, `parseTemplateMemberFunction`, lambdas, ctors/dtors) and catalog every divergence (implicit params, attributes, diagnostics). Produce a checklist doc in `docs/audits/`.
2. **Instrumentation**: add logging/trace toggles (compile-time guarded) to capture current scope/IR shape for regression comparison.
3. **Feature flag**: stub `USE_FUNCTION_PIPELINE_V2` macro and plumbing so new code can coexist without risking mainline stability.

### Phase 1 — Shared Shape & Context (2 weeks)
1. Implement `FunctionShape` + `FunctionContext` structs and builder APIs; wire them only for non-template free functions under the feature flag.
2. Port reusable helpers (parameter parsing, attribute folding, requires-clause parsing) to accept `FunctionContext` and add adapter shims so old callers continue to compile.
3. Extend doctests to cover the refactored free-function path (baseline: constructors, constexpr, noexcept) ensuring zero behavior drift.

### Phase 2 — Member Functions (2 weeks)
1. Teach the shape builder how to derive implicit `this`, access specifiers, and defaulted special members.
2. Route `parseMemberFunction` and `bindMemberFunction` through the new pipeline; behind the flag, emit both old and new IR for comparison using `build_and_dump.bat`.
3. Update regression fixtures (`tests/Reference/`) for member functions with attributes/constraints to ensure parity.

### Phase 3 — Template Coordination (3 weeks)
1. Implement `TemplateFunctionCoordinator` + unified `TemplateBodyCache`; adopt them for free templates first, keeping existing instantiation maps alive until parity is proven.
2. Introduce policy objects (`PrimaryTemplatePolicy`, `PartialSpecializationPolicy`, `InstantiationPolicy`) and migrate binding logic.
3. Extend coverage: add doctests covering matrix `[free/member] × [primary/partial] × [requires/no requires]` with attribute propagation expectations.

### Phase 4 — Unified Lowering (1 week)
1. Refactor IR emission so `emitFunctionLike(const FunctionContext&)` handles parameter layout, prolog/epilog, and calling convention toggles.
2. Delete per-kind emission helpers once dumps confirm no drift beyond intentional naming differences.

### Phase 5 — Cleanup & Rollout (1 week)
1. Remove legacy entry points, fold feature flag to default-on, and update developer docs (see next section).
2. Run full Windows + WSL test matrices, ensure benchmarks show no regressions.

## Workstreams & Ownership
- **Parser/Core AST** (Owner: Parser WG): Phase 0-2 tasks; responsible for `FunctionShape` builder correctness.
- **Templates & Semantics** (Owner: Templates WG): Phase 3 deliverables plus template-specific testing.
- **IR/Codegen** (Owner: Backend WG): Phase 4 plus regression tooling (build_and_dump harness updates).
- **Tooling & QA** (Owner: Infra WG): Maintain feature flag toggles, CI gating, perf dashboards.

Weekly sync between WGs should review:
- Checklist burn-down (discovery items closed?).
- Diff noise in IR dumps (should trend downward once unified).
- Perf + compile-time deltas (track via benchmark scripts already in repo).

## Developer Enablement
1. New developer docs: add `docs/function_pipeline.md` summarizing the architecture; cross-link from `TEMPLATE_*` docs.
2. Template for adding future function traits: provide example of extending `FunctionShape` with `CoroutineTraits` without touching parser or IR.
3. Linting rule (clang-tidy/custom) to forbid new uses of legacy entry points once feature flag is default-on.

## Test & Validation Plan
- Expand `tests/FlashCppTest/FlashCppTest.cpp` with cases that exercise identical constructs across free/member/template combinations (attributes, `constexpr`, default args) to detect regressions.
- Add fixture inputs under `tests/Reference/` covering the newly unified behaviors.
- Use `build_flashcpp.bat` + `link_and_run_test_debug.bat` on Windows and `make test CXX=clang++` on WSL for cross-compiler validation.
- Leverage `build_and_dump.bat` when IR-level differences are suspected; the shared lowering path should reduce diff surface.

## Risks & Mitigations
- **Scope/lookup regressions**: mitigate via the descriptor/context approach—ensuring scopes are explicit rather than implicit flags.
- **Template instantiation explosions**: the unified cache must retain memoization currently scattered across template code.
- **Timeline creep**: stage the migration behind feature toggles (e.g., `USE_FUNCTION_PIPELINE_V2`) to enable gradual rollout.

## Success Criteria
- Any fix for function parsing requires touching a single pipeline, not four variants.
- Green test matrix with additional coverage for cross-cutting scenarios.
- Documented FunctionShape + pipeline in the developer docs, reducing onboarding time.
- Ability to add new function traits (coroutines, `constexpr` lambdas, etc.) via isolated policy updates instead of duplicating logic.
