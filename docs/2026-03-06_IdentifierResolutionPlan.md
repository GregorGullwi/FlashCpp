# Identifier Resolution Refactoring: Move Name Binding to Parse Time

Move identifier name resolution from codegen (runtime multi-table lookups on every use) to parse time (semantic analysis), so `IdentifierNode` carries its resolved binding. This eliminates `detectGlobalOrStaticVar()` and the ~4-table lookup pattern scattered across codegen.

> [!IMPORTANT]
> This is a **phased refactoring**. Each phase is independently shippable and testable. Phase 1 alone eliminates the worst offender (`detectGlobalOrStaticVar`).

> [!IMPORTANT]
> For **new standards-compliance features** added by this plan, follow **TDD style**: add the targeted test file first, confirm it fails or is unsupported with the current implementation, then implement the feature, and finally rerun the targeted test plus the broader regression suite.

> [!IMPORTANT]
> The **phase structure is for planning/documentation only**. Do **not** add phase comments like `Phase 1`, `Phase 2`, etc. into the production source code. Keep the code comments focused on semantics and behavior, not project-management milestones.

> [!IMPORTANT]
> To be **C++20 lookup-compliant after the refactor**, we must be careful not to merely cache the compiler's current lookup behavior. The binding helper must preserve standard lookup semantics for:
> - ordinary unqualified lookup, including names introduced by `using` declarations/directives
> - overload-set formation for functions
> - argument-dependent lookup (ADL), including hidden friends
> - point-of-declaration rules inside initializers
> - template-dependent contexts, especially dependent base classes

> [!NOTE]
> The phases below are **dependency and review boundaries**, not evenly sized implementation sessions. Some phases are small enough for one session (`Phase 2`, `Phase 5`), while others are better split across multiple sessions (`Phase 0`, `Phase 1`, `Phase 4`).

## Implementation Progress

| Phase | Status | Branch commit(s) | Notes |
|-------|--------|-----------------|-------|
| Phase 0A — using/overload-set semantics | ✅ Done | (no code change needed) | Tests `test_using_directive_overload_merge_ret0` and `test_using_declaration_overload_set_ret0` already pass |
| Phase 0B — point-of-declaration fix | ✅ Done | `c264c2f3` | `sizeof(x)` in own initializer now returns 4; pre-inserts stub VarDecl before parsing initializer; `sizeof` disambiguation fixed; codegen registers var before evaluating initializer |
| Phase 1A — IdentifierBinding + IdentifierNode + createBoundIdentifier | ✅ Done | `b8ca5961` | `IdentifierBinding` enum (12 variants) added to `AstNodeTypes_DeclNodes.h`; `IdentifierNode` extended; `createBoundIdentifier()` added to `Parser.h`; 16 creation sites in `Parser_Expr_PrimaryExpr.cpp` updated; `get_scope_type_of_symbol()` added to `SymbolTable.h`; typo `idenfifier_token` → `identifier_token` fixed (308 occurrences) |
| Phase 1B — Remove detectGlobalOrStaticVar, migrate codegen | ✅ Done | `ecb1ddf6`, `77ab43db` | `GlobalStaticVarInfo` and `detectGlobalOrStaticVar()` deleted; `StaticLocal`/`StaticMember` bindings set in `createBoundIdentifier()`; all codegen paths (`generateIdentifierIr`, assignment, compound-assignment, inc/dec, `analyzeAddressExpression`) switch on `binding()`; grep for `detectGlobalOrStaticVar` = 0 |
| Phase 2 — Lambda capture bindings | ⬜ Pending | — | |
| Phase 3 — Template two-phase lookup | ⬜ Pending | — | See expanded section below for newly identified gaps |
| Phase 4 — Unqualified function lookup + ADL | ⬜ Pending | — | |
| Phase 5 — ConstExprEvaluator fast paths | ⬜ Pending | — | |

> [!NOTE]
> All 1327 tests pass on branch `copilot/identifier-refactor` after Phase 1B (rebased on `3e4c591c`).

## Recommended Session Breakdown

If implementation is done over multiple focused sessions, use the phases as milestones but split the larger ones like this:

1. **Session 1 — Phase 0A**
   - Preserve overload sets in lookup
   - Fix `using` declaration/directive import behavior
   - Add failing tests first
2. **Session 2 — Phase 0B**
   - Fix point-of-declaration behavior in variable initializers
   - Add/verify targeted tests first
3. **Session 3 — Phase 1A**
   - Add `IdentifierBinding`
   - Extend `IdentifierNode`
   - Add `createBoundIdentifier()`
   - Bind locals, globals, static locals, enum constants
4. **Session 4 — Phase 1B**
   - Bind member-related cases
   - Migrate codegen away from `detectGlobalOrStaticVar()`
   - Re-run targeted regression tests
5. **Session 5 — Phase 2**
   - Lambda capture bindings
   - Lambda-focused regression tests
6. **Session 6 — Phase 3**
   - Template instantiation binding
   - Dependent-context guardrails
   - Dependent-base regression tests
7. **Session 7 — Phase 4A**
   - Ordinary function candidate collection
   - Overload-set handling for unqualified calls
8. **Session 8 — Phase 4B**
   - ADL
   - Hidden friends
   - ADL-focused regression tests
9. **Session 9 — Phase 5**
   - Constexpr consumer cleanup / fast paths
   - Final regression verification

> [!NOTE]
> In other words: the current phases are a good **implementation order**, but they should not be assumed to be equal-duration sessions.

## Phase 0 — Semantic Guardrails Before Caching Results

Before moving more resolution work to parse time, tighten the underlying lookup semantics so we do not freeze incorrect answers into `IdentifierNode`.

#### [MODIFY] [SymbolTable.h](file:///c:/Projects/FlashCpp2/src/SymbolTable.h)
- `lookup()` / `lookup_all()` currently favor the **first** hit from some scopes/using-directive paths. For standard behavior, function lookup must preserve or merge the reachable overload set rather than short-circuiting on the first namespace that happens to contain a name.
- `using` declarations must not collapse an imported overload set to a single symbol. If `using ns::f;` names multiple overloads, the imported name still denotes the overload set.
- Keep ordinary unqualified lookup as the source of truth for parse-time binding. `createBoundIdentifier()` should consume this API, not reimplement a narrower local/global/member search order.

#### [MODIFY] [Parser_Decl_FunctionOrVar.cpp](file:///c:/Projects/FlashCpp2/src/Parser_Decl_FunctionOrVar.cpp)
- Respect **point of declaration** for variables: the declared name becomes visible immediately after its declarator and before its initializer is parsed.
- Today local/global variables are inserted after the initializer has already been parsed. That is not fully standard-compliant for cases like self-reference in the initializer or other semantic checks that depend on the declared name already being in scope.
- Any refactor that binds identifiers at parse time should fix or explicitly account for this first, otherwise it may lock in the wrong binding in initializer expressions.

## Phase 1 — AST + Core Bindings (Local, Global, StaticLocal, StaticMember)

### AST Node Changes

#### [MODIFY] [AstNodeTypes_DeclNodes.h](file:///c:/Projects/FlashCpp2/src/AstNodeTypes_DeclNodes.h)

Add enum and extend `IdentifierNode`:

```cpp
enum class IdentifierBinding : uint8_t {
    Unresolved,         // Not yet resolved (default, templates, deferred)
    Local,              // Local variable or function parameter
    Global,             // Global variable (file scope / namespace scope)
    StaticLocal,        // static local variable inside a function
    StaticMember,       // static data member of a struct/class
    NonStaticMember,    // non-static data member (implicit this->member)
    CapturedByValue,    // Lambda [x] capture
    CapturedByRef,      // Lambda [&x] capture
    CapturedThis,       // Lambda [this] capture
    CapturedCopyThis,   // Lambda [*this] capture
    EnumConstant,       // Enumerator value
    Function,           // Function name (not a variable)
    TemplateParameter,  // Non-type or type template parameter reference
};
```

> [!NOTE]
> `TemplateParameter` is separate from `Unresolved` so that call sites can distinguish "genuinely unknown" from "known to be a template parameter, defer to instantiation". Currently `lookup_symbol()` already returns `TemplateParameterReferenceNode` for both type and non-type template parameters (guarded by `parsing_template_body_` + `current_template_param_names_`), so `createBoundIdentifier()` leaves these as `Unresolved`. Adding a dedicated variant makes the intent explicit and allows codegen to assert correctly.

> [!NOTE]
> `Function` must mean **"ordinary lookup found one or more callable declarations"**, not "overload resolution is finished". Do **not** use `resolved_name_` to pretend an overloaded callee has already been selected. ADL and overload resolution still happen later at the call site.

Extend `IdentifierNode` (currently lines 1195–1206):

```cpp
class IdentifierNode {
public:
    explicit IdentifierNode(Token identifier) : identifier_(identifier) {}

    std::string_view name() const { return identifier_.value(); }
    StringHandle nameHandle() const { return identifier_.handle(); }
    std::optional<Token> try_get_parent_token() { return parent_token_; }

    // Binding resolution
    IdentifierBinding binding() const { return binding_; }
    void set_binding(IdentifierBinding b) { binding_ = b; }
    StringHandle resolved_name() const { return resolved_name_; }
    void set_resolved_name(StringHandle h) { resolved_name_ = h; }
    bool is_resolved() const { return binding_ != IdentifierBinding::Unresolved; }

private:
    Token identifier_;
    std::optional<Token> parent_token_;
    IdentifierBinding binding_ = IdentifierBinding::Unresolved;
    StringHandle resolved_name_; // mangled/qualified name for static locals, static members, globals
};
```

> [!NOTE]
> Size impact: +2 bytes (1 enum + 1 `StringHandle` which is likely already padded). `IdentifierNode` is lightweight by design.

### Parser Binding Logic

#### [MODIFY] [Parser_Expr_PrimaryExpr.cpp](file:///c:/Projects/FlashCpp2/src/Parser_Expr_PrimaryExpr.cpp)

There are **~40 sites** that create `IdentifierNode(token)`. Rather than modifying each one, introduce a helper:

```cpp
// In Parser.h or ParserTypes.h
IdentifierNode createBoundIdentifier(Token token);
```

This helper performs the binding lookup once:

1. **Run ordinary unqualified lookup first** using the existing symbol-table machinery at the exact point of use.
2. If the winning declaration is a **local / parameter** → `Local`
3. If it is a **function-scope `static` variable** → `StaticLocal`, compute mangled name, store in `resolved_name_`
4. If it is a **namespace/global variable** → `Global`
5. If it is an **enumerator** → `EnumConstant`
6. If ordinary lookup finds **function declarations / overloads** → `Function` (metadata only; overload resolution and ADL stay at the call site)
7. If ordinary lookup found nothing, and we are in a non-dependent member-function context, check **current struct static members** → `StaticMember`
8. If ordinary lookup found nothing, and we are in a non-dependent member-function context, check **implicit non-static members** via `gLazyMemberResolver` → `NonStaticMember`
9. **Otherwise** → leave as `Unresolved` (safe fallback, templates/dependent lookup/call-site logic still handle it)

> [!WARNING]
> **Forward references in class bodies** ([class.mem]/2): Inside class definitions, member function bodies can refer to members declared later. Your parser already defers member function body parsing until the class is complete — so at binding time, the full member list is available. No special handling needed.

> [!WARNING]
> **Name shadowing**: Binding must happen at the **point of use** (when the `IdentifierNode` is created during expression parsing), not at declaration time. The scope stack at that point correctly reflects shadowing. A global `n` shadowed by a local `n` will bind to `Local` because `gSymbolTable.lookup()` searches innermost scope first.

> [!WARNING]
> **Do not bypass `using` semantics**: `createBoundIdentifier()` must not jump straight to "check locals, then globals, then members" if that skips `using` declarations/directives or overload-set formation. Those are part of ordinary unqualified lookup.

### Codegen Cleanup

#### [MODIFY] [AstToIr.h](file:///c:/Projects/FlashCpp2/src/AstToIr.h)
- Remove `GlobalStaticVarInfo` struct (line 62–67)
- Remove `detectGlobalOrStaticVar()` declaration (line 71)

#### [MODIFY] [CodeGen_Expr_Operators.cpp](file:///c:/Projects/FlashCpp2/src/CodeGen_Expr_Operators.cpp)
- **Delete** `detectGlobalOrStaticVar()` definition (lines 3–56)
- **Lines 484–524** (assignment `=` to global/static): Codegen currently relies on `GlobalStaticVarInfo::type` and `size_in_bits` to emit `GlobalStore`. By updating `generateIdentifierIr` to properly return `{type, size, resolved_name}` for globals in `ExpressionContext::LValueAddress`, we can either:
  1. Call `visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress)` to obtain the type, size, and store name handle directly, eliminating the need to look up type metadata manually.
  2. Or, better yet, unify the global store logic with the general assignment handler.
- **Lines 530–582** (compound assignment `+=` etc. to global/static): Same as above. The type and size will be fetched by doing a regular `visitExpressionNode()` call on the LHS.

#### [MODIFY] [CodeGen_Expr_Conversions.cpp](file:///c:/Projects/FlashCpp2/src/CodeGen_Expr_Conversions.cpp)
- **Line 1694**: Replace `detectGlobalOrStaticVar(std::get<IdentifierNode>(operandExpr).name())` with binding check on increment/decrement handler

#### [MODIFY] [CodeGen_Expr_Primitives.cpp](file:///c:/Projects/FlashCpp2/src/CodeGen_Expr_Primitives.cpp)
- **`generateIdentifierIr()`** (lines 373–428): Currently does manual `static_local_names_.find()` + `symbol_table.lookup()` cascade. Replace with switch on `identifierNode.binding()`:
  - `StaticLocal` → emit `GlobalLoad` with `resolved_name()`
  - `Global` → emit `GlobalLoad` with `resolved_name()` (or nameHandle)
  - `Local` → emit `Load` from symbol table
  - `Unresolved` → fall back to current multi-lookup logic (backward compat)

---

## Phase 2 — Lambda Capture Bindings

#### [MODIFY] [Parser_Expr_PrimaryExpr.cpp](file:///c:/Projects/FlashCpp2/src/Parser_Expr_PrimaryExpr.cpp)
- When parsing inside a lambda body (parser knows this via lambda context), if an identifier matches a capture:
  - `CapturedByValue`, `CapturedByRef`, `CapturedThis`, `CapturedCopyThis`

#### [MODIFY] [CodeGen_Expr_Operators.cpp](file:///c:/Projects/FlashCpp2/src/CodeGen_Expr_Operators.cpp)
- **Lines 412–445**: The special-case block for `captured-by-reference assignment` can be replaced by checking `lhs_ident.binding() == IdentifierBinding::CapturedByRef`
- **Lines 380–410**: The special-case block for `implicit member assignment` can be replaced by checking `lhs_ident.binding() == IdentifierBinding::NonStaticMember`

---

## Phase 3 — Template Two-Phase Lookup (C++20 [temp.res])

> [!CAUTION]
> **Dependent names cannot be bound at parse time.** In template definitions, names that depend on template parameters must remain `Unresolved` (or `TemplateParameter`) until instantiation.

```cpp
template<typename T>
void foo(T& obj) {
    obj.x = 42;    // dependent — leave Unresolved
    n += 7;        // non-dependent — CAN bind at parse time
    static int s;  // non-dependent — CAN bind as StaticLocal
}
```

### How the current parser tracks template context

`Parser.h` uses two state fields together:
- `bool parsing_template_body_` (line 452) — set to `true` while inside a template definition.
- `InlineVector<StringHandle, 4> current_template_param_names_` (line 433) — holds the names of all active template parameters (both type parameters like `typename T` and non-type parameters like `int N`).

`lookup_symbol()` (line 1151) passes `current_template_param_names_` to `gSymbolTable.lookup()`, which returns a `TemplateParameterReferenceNode` when the name matches a template parameter. `createBoundIdentifier()` checks for this and leaves those names `Unresolved`. This correctly handles both type and non-type template parameters.

### Known gaps and required work

#### [RISK] `parsing_template_body_` is a `bool`, not a depth counter

Setting/clearing happens at many call sites across `Parser_Templates_Class.cpp`, `Parser_Templates_Function.cpp`, `Parser_Templates_Lazy.cpp`, and others. With nested class templates (a template defined inside another template), a naive set/clear pattern can leave the flag in the wrong state after backtracking.

**Fix**: Replace with a `size_t parsing_template_depth_` counter (increment on enter, decrement on exit via RAII guard). `parsing_template_body_` becomes `parsing_template_depth_ > 0`. This ensures correctness for arbitrarily nested templates.

#### [MODIFY] `createBoundIdentifier()` — add `TemplateParameter` variant

Currently `TemplateParameterReferenceNode` hits the catch-all `return node` (Unresolved). Change to set `IdentifierBinding::TemplateParameter` explicitly so consumers can distinguish "deferred — instantiation will resolve" from "genuinely unknown". Update `AstNodeTypes_DeclNodes.h` with the new enum variant (see Phase 1 section).

#### [RISK] Dependent base classes — `has_deferred_base_classes` not checked

`StructTypeInfo` has a `has_deferred_base_classes` flag (set in `Parser_Templates_Class.cpp`). When a class template has a dependent base (e.g., `template<T> struct D : T`), unqualified names in its member-function bodies might be provided by `T`. `createBoundIdentifier()` does **not** currently check `has_deferred_base_classes`. If an unqualified name fails local lookup and ordinary lookup also misses, the helper should check this flag before falling through to `NonStaticMember` or `Unresolved` — and stay `Unresolved` so codegen's lookup-at-instantiation fallback can find the name in the concrete base.

**Fix**: Before the `NonStaticMember` check in `createBoundIdentifier()`, if the current struct has `has_deferred_base_classes == true`, skip the implicit member search and return `Unresolved`.

#### [MODIFY] Re-binding after instantiation — substitution files do not create new IdentifierNodes

`Parser_Templates_Inst_Substitution.cpp` and `Parser_Templates_Inst_Deduction.cpp` do **not** contain `IdentifierNode(token)` constructor calls. They transform/copy existing IdentifierNodes from the template definition's AST. The original plan's instruction "call `createBoundIdentifier()` at creation sites in `Parser_Templates_Substitution.cpp` lines 57, 96, 171, 493, 1226" is therefore incorrect.

The correct approach:
1. After substitution produces a concrete expression tree, walk all `IdentifierNode` children that still have `binding() == Unresolved` (or `TemplateParameter`).
2. For each, call `createBoundIdentifier()` (or a post-instantiation equivalent that has access to the now-concrete type information).
3. Bindings that resolve at this point (e.g., a global that was shadowed by a template param) receive their final binding; anything still unknown stays `Unresolved` for codegen's fallback.

#### [MODIFY] Files

- `src/Parser.h` — Replace `parsing_template_body_` bool with a `parsing_template_depth_` counter; update `lookup_symbol()` test accordingly; add `TemplateParameter` case to `createBoundIdentifier()`; add dependent-base guard before `NonStaticMember` check.
- `src/AstNodeTypes_DeclNodes.h` — Add `TemplateParameter` to `IdentifierBinding` enum.
- `src/Parser_Templates_Inst_Substitution.cpp` — After each substitution call that produces a new expression tree, walk `IdentifierNode`s and re-bind `Unresolved`/`TemplateParameter` ones via `createBoundIdentifier()`.
- `src/Parser_Templates_Class.cpp` and `Parser_Templates_Function.cpp` — Replace all `parsing_template_body_ = true/false` with RAII depth-counter guards.

#### No change needed for already-resolved non-dependent names

Names already bound in Phase 1 (e.g., a global variable referenced in a template function that is not dependent on any template parameter) keep their binding through instantiation. The `Unresolved` default remains the safe fallback for codegen.

#### Compliance notes for dependent member lookup

- In templates with a **dependent base class**, unqualified names must **not** be eagerly bound as `NonStaticMember` during primary-template parsing — a later instantiation's base might provide the name.
- `this->member`, `Base<T>::member`, and other explicitly dependent forms are resolved at instantiation/substitution time, not during the first parse of the template definition.
- `createBoundIdentifier()` must treat "implicit member lookup inside a class with `has_deferred_base_classes`" as a reason to stay `Unresolved`.

---

## Phase 4 — Unqualified Function Lookup + ADL (C++20 [basic.lookup.argdep])

This is the largest missing correctness area if we want the refactor to be genuinely standard-compliant rather than just faster.

#### [MODIFY] [Parser_Expr_PrimaryExpr.cpp](file:///c:/Projects/FlashCpp2/src/Parser_Expr_PrimaryExpr.cpp)
- For an unqualified call like `f(args...)`, ordinary lookup alone is not enough.
- Build candidate sets from:
  1. ordinary lookup / `using` declarations / `using namespace`
  2. ADL based on the associated namespaces/classes of the argument types
- If the callee is spelled as a plain identifier, treat parse-time binding as metadata (`Function` or `Unresolved`), not as the final selected declaration.

#### [MODIFY] [SymbolTable.h](file:///c:/Projects/FlashCpp2/src/SymbolTable.h)
- Add a way to collect the **ordinary lookup overload set** without short-circuiting.
- Add or expose a helper that can collect ADL candidates from associated namespaces/classes.
- Hidden friends should become visible through ADL even when ordinary lookup cannot find them.

#### [MODIFY] [Overload resolution path(s)]
- Wherever `lookup_all()` + `resolve_overload()` currently handles unqualified calls, extend it to combine ordinary lookup candidates with ADL candidates before final selection.
- ADL must be suppressed in cases where the standard says ordinary lookup already found a blocking non-function declaration.

---

## Phase 5 — ConstExprEvaluator Consumer

#### [MODIFY] [ConstExprEvaluator_Members.cpp](file:///c:/Projects/FlashCpp2/src/ConstExprEvaluator_Members.cpp)
- ~20 sites doing `std::get<IdentifierNode>(expr).name()` followed by manual lookups can benefit from checking `.binding()` first for fast-path evaluation
- This is a performance optimization, not a correctness fix — lower priority

---

## Files Summary

| Phase | File | Change |
|-------|------|--------|
| 1 ✅ | `AstNodeTypes_DeclNodes.h` | Added `IdentifierBinding` enum (12 variants + `TemplateParameter` to add), extended `IdentifierNode` |
| 1 ✅ | `Parser.h` | Added `createBoundIdentifier()`, `get_scope_type_of_symbol()` |
| 1 ✅ | `Parser_Expr_PrimaryExpr.cpp` | 16 creation sites updated to `createBoundIdentifier()`; typo `idenfifier_token` fixed |
| 1 ✅ | `AstToIr.h` | Removed `GlobalStaticVarInfo`, `detectGlobalOrStaticVar()` |
| 1 ✅ | `CodeGen_Expr_Operators.cpp` | Removed `detectGlobalOrStaticVar()`; all assignment paths use `binding()` |
| 1 ✅ | `CodeGen_Expr_Conversions.cpp` | Inc/dec and `analyzeAddressExpression` use `binding()` |
| 1 ✅ | `CodeGen_Expr_Primitives.cpp` | `generateIdentifierIr()` switches on `binding()` |
| 2 | `Parser_Expr_PrimaryExpr.cpp` | Bind lambda captures |
| 2 | `CodeGen_Expr_Operators.cpp` | Remove ad-hoc lambda capture checks |
| 3 | `AstNodeTypes_DeclNodes.h` | Add `TemplateParameter` to `IdentifierBinding` enum |
| 3 | `Parser.h` | Replace `parsing_template_body_` bool with depth counter; add `TemplateParameter` case and dependent-base guard to `createBoundIdentifier()` |
| 3 | `Parser_Templates_Class.cpp` | Replace `parsing_template_body_` set/clear with RAII depth-counter guards |
| 3 | `Parser_Templates_Function.cpp` | Same RAII depth-counter guard |
| 3 | `Parser_Templates_Inst_Substitution.cpp` | After substitution: walk `Unresolved`/`TemplateParameter` IdentifierNodes and re-bind via `createBoundIdentifier()` |
| 4 | `SymbolTable.h` | Preserve overload-set formation and add ADL support |
| 4 | `Parser_Expr_PrimaryExpr.cpp` | Combine ordinary lookup with ADL for unqualified calls |
| 5 | `ConstExprEvaluator_Members.cpp` | Use binding for fast-path evaluation |

Also touched (secondary — `IdentifierNode` creation sites):
- `Parser_Expr_BinaryPrecedence.cpp`, `Parser_FunctionHeaders.cpp`, `Parser_Decl_StructEnum.cpp`, `CodeGen_Stmt_Control.cpp`, `Parser_Expr_PostfixCalls.cpp`

## Verification Plan

### Automated Tests
- **TDD rule for new feature work**:
  1. Add the focused regression test first
  2. Verify it fails (or otherwise demonstrates the missing behavior) on the current compiler
  3. Implement the feature/fix
  4. Rerun the focused test until it passes
  5. Run the relevant broader regression set to ensure no lookup regressions were introduced
- `make` / MSVC build — must compile clean
- Full test suite (`make test` or equivalent) — all existing tests must pass
- Key test categories: static locals, globals, compound assignment on globals, lambda captures, template instantiations, struct member access, using-directive/declaration lookup, overload-set import, ADL, hidden friends, dependent-base lookup

### Suggested New Regression Tests
- These should be added **before** implementing the corresponding missing feature so they serve as proof that the current compiler is incomplete and as verification once the implementation lands.
- `tests/test_adl_namespace_function_ret0.cpp`
  - `namespace Lib { struct X {}; int run(X) { return 0; } }`
  - `int main() { Lib::X x; return run(x); }`
  - Standard-valid via ADL; likely fails today if only ordinary lookup is used.
- `tests/test_adl_hidden_friend_ret0.cpp`
  - Hidden friend declared inside a class and called as `tag(obj)`.
  - Standard-valid only through ADL; a good guard against missing hidden-friend support.
- `tests/test_using_directive_overload_merge_ret0.cpp`
  - Two namespaces each provide an overload of the same function; `using namespace` imports both.
  - Call should select the best overload from the **merged** set, not from the first namespace searched.
- `tests/test_using_declaration_overload_set_ret0.cpp`
  - `using ns::f;` where `ns::f` is overloaded.
  - Verifies the imported name still denotes the full overload set.
- `tests/test_lambda_this_implicit_member_ret0.cpp`
  - Member function with `[this]` or `[*this]` lambda returning `value` without explicit `this->value`.
  - Guards the interaction between capture binding and implicit member lookup.
- `tests/test_dependent_base_this_lookup_ret0.cpp`
  - Template derives from `Base<T>` and uses `this->value` / `this->func()`.
  - Confirms dependent member lookup is deferred correctly until instantiation.
- `tests/test_initializer_point_of_declaration_ret4.cpp`
  - A deterministic point-of-declaration case such as `int x = sizeof(x); return x;`
  - Useful to verify the variable is considered in scope for its own initializer according to the standard, even before initialization is complete.

- `tests/test_template_nondependent_global_ret0.cpp`
  - A function template that references a non-dependent global variable and a non-dependent free function.
  - Verifies non-dependent names are bound at parse time (first-phase lookup) and not accidentally re-looked up at instantiation.
  - Example: `int g = 0; void helper() {} template<typename T> int f(T) { helper(); return g; } int main() { return f(42); }`
- `tests/test_template_nontype_param_shadows_global_ret0.cpp`
  - A non-type template parameter with the same name as a global variable.
  - `int N = 99; template<int N> int f() { return N; } int main() { return f<0>(); }`
  - Confirms the template parameter takes priority over the global — binding must be `TemplateParameter`/`Unresolved`, not `Global`.
- `tests/test_template_static_local_ret1.cpp`
  - A function template with a `static` local variable; called multiple times.
  - Verifies `StaticLocal` binding survives instantiation and the static storage is shared across calls.
- `tests/test_template_dependent_base_unqualified_ret0.cpp`
  - `template<typename Base> struct D : Base { int f() { return value; } }` where `Base` supplies `value`.
  - Confirms unqualified `value` in `D::f()` is deferred (stays `Unresolved`) during primary-template parsing and is resolved correctly at instantiation. Currently likely fails.
- `tests/test_template_rebind_after_instantiation_ret42.cpp`
  - A template that uses a name that is `Unresolved` in the template definition but becomes `Global` once instantiated (e.g., a global variable introduced after the template is defined but before it is instantiated).
  - Tests that post-instantiation re-binding via the AST walk correctly updates the binding.

### Manual Verification
- Compile small programs with `static` locals, globals, lambda `[&x]` captures, `using namespace`, ADL calls, and hidden-friend calls; verify identical or improved behavior relative to the intended standard semantics
- Grep for remaining `detectGlobalOrStaticVar` references — must be zero (achieved in Phase 1B)
- Grep for `parsing_template_body_` assignment — must be zero after Phase 3 (all replaced with depth-counter guards)
