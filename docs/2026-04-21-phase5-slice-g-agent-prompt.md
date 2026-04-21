# Phase 5 Slice G Agent Prompt: Explicit ODR-Use Marking + Instantiated-Struct List

**Date Created:** 2026-04-21
**Status:** Ready for agent execution
**Objective:** Delete `AstToIr::materializeLazyMemberIfNeeded` by making ODR-use explicit in the lazy-member registry and splitting instantiated structs into their own list as described in docs/2026-04-21-phase5-slice-g-analysis.md

---

## Problem Statement

Phase 5 Slice F landed an end-of-sema lazy-member drain that preempts the codegen struct-visitor for AST-reachable instantiations. However, a small residual set of instantiated structs (held only through `StructTypeInfo` / lazy-registry references) is not AST-reachable, so the drain cannot touch them. The codegen forwarder `AstToIr::materializeLazyMemberIfNeeded` remains as a safe fallback for those cases.

The architectural root cause: **the lazy-member registry has no explicit "this member has been ODR-used" signal**. Currently, `needsInstantiation(struct, member)` returns true for every cloned member of every instantiation, regardless of whether anything actually calls it. The "what to materialize" decision is implicit: codegen struct-visitor iterates reachable structs and emits every `needsInstantiation=true` member, which accidentally proxies for "user wrote this in the source" or "a call site explicitly materialized it".

Attempting to generalize the drain to all instantiated structs (not just AST-reachable ones) fails because it over-materializes SFINAE-probed template arguments (e.g., `pair<const int, int>` from `is_swappable<pair<const int, int>>`), whose member bodies may be ill-formed by design.

**Solution:** Replace the implicit proxy with explicit state:
1. Add an `odr_used` bit to lazy-member registry entries.
2. Update sema annotation sites (Slices A-E, constexpr, etc.) to call `markOdrUsed(struct, member)` at the moment they identify a target.
3. Change the drain filter from `needsInstantiation` to `odr_used && !materialized`.
4. Stop using `parser_.get_nodes()` as a proxy for "safe to emit all members"; instead, have codegen struct-visitor skip instantiated structs and rely on sema's drain.
5. Split `ast_nodes_` into user-written `user_source_nodes_` and append-only `instantiated_class_template_nodes_` to eliminate accidental double-push bugs.

This unblocks deletion of the forwarder and makes the invariant **principled and non-fragile**: the drain materializes exactly what sema marked ODR-used, no more, no less.

---

## High-Level Approach

### Part A: Extend LazyMemberInstantiationRegistry with ODR-use tracking

**File:** `src/TemplateRegistry_LazyMember.h` / `src/TemplateRegistry_LazyMember.cpp`

1. Add a new method to `LazyMemberInstantiationRegistry`:
   ```cpp
   void markOdrUsed(StringHandle struct_name, StringHandle member_name, 
                    std::optional<bool> is_const_member = std::nullopt);
   ```

2. Internally, store ODR-use state alongside each lazy-member entry. The registry entry should track:
   - `is_instantiated`: existing bit (entry was materialized).
   - `is_odr_used`: new bit (something explicitly marked this as ODR-used).

3. Add a corresponding query:
   ```cpp
   bool isOdrUsed(StringHandle struct_name, StringHandle member_name, 
                  std::optional<bool> is_const_member = std::nullopt) const;
   ```

4. Clarify semantics in comments:
   - Every cloned member starts with `is_instantiated=false, is_odr_used=false`.
   - `markOdrUsed` sets `is_odr_used=true` (idempotent).
   - The drain only materializes entries where `is_odr_used && !is_instantiated`.
   - SFINAE-probed instantiations are never marked ODR-used, so their bodies stay lazily ill-formed.

**Example of new entry state:**
```cpp
struct LazyMemberEntry {
    LazyMemberFunctionInfo info;
    bool is_instantiated = false;  // existing
    bool is_odr_used = false;      // new — set when a sema site explicitly needs the body
};
```

---

### Part B: Update sema annotation sites to call `markOdrUsed`

These sites already identify a target and materialize it (Slices A-E). Extend each to mark ODR-use first.

**Sites to update** (in order of impact):

1. **`SemanticAnalysis::tryAnnotateConversion`** (slice A)
   - When a conversion operator is selected: call `lazy_registry.markOdrUsed(struct_name, "operator_T", is_const)`.
   - Then call `ensureMemberFunctionMaterialized` as today.

2. **`SemanticAnalysis::tryAnnotateCallArgConversionsImpl`** (slices B-C)
   - When a direct-call target or member-call target is selected: call `lazy_registry.markOdrUsed(...)` before materialization.
   - One call per selected function, including free-function friends.

3. **`ConstExpr::Evaluator::find_current_struct_member_function_candidate`** (slice D)
   - When returning a candidate for constexpr evaluation: call `lazy_registry.markOdrUsed(...)`.

4. **Deferred-queue seeding sites** (slice E)
   - In `IrGenerator_Visitors_Decl.cpp` struct-visitor loop: when pushing a lazy member into deferred queue, call `markOdrUsed` first.
   - In `IrGenerator_Call_Direct.cpp` `queueDeferredMemberFunctions`: same.

5. **Manual vtable seed** (if any call to `materializeLazyMemberIfNeeded` from codegen vtable logic)
   - Replace with `markOdrUsed` call in sema, then rely on drain.

**Key pattern:**
```cpp
// OLD (Slice A/B/C/D pattern):
auto materialized = sema->ensureMemberFunctionMaterialized(struct_name, member_name, is_const);

// NEW:
sema->lazy_registry.markOdrUsed(struct_name, member_name, is_const);  // signal ODR-use
auto materialized = sema->ensureMemberFunctionMaterialized(struct_name, member_name, is_const);
```

Actually, to keep it clean, extend `ensureMemberFunctionMaterialized` to take an optional `mark_odr_used` parameter (default true), so the call can remain single-line:

```cpp
auto materialized = sema->ensureMemberFunctionMaterialized(
    struct_name, member_name, is_const, /*mark_odr_used=*/true);
```

And inside that method, call `markOdrUsed` before the actual materialization.

---

### Part C: Update `SemanticAnalysis::drainLazyMemberRegistry` to filter by `odr_used`

**File:** `src/SemanticAnalysis.cpp`

Current drain walks `parser_.get_nodes()` and materializes every `needsInstantiation=true` member.

New drain:
1. Continue walking `parser_.get_nodes()` (user-written structs).
2. **Also** walk `parser_.getInstantiatedClassTemplateNodes()` (the side list from earlier work in this session, which we're keeping).
3. For each struct, iterate its members and materialize only those where `odr_used && !instantiated`.

This is subtle: the drain now handles **all** ODR-used members, including those from instantiated-but-unreachable structs. The codegen forwarder becomes an obsolete safety net (only if sema *forgot* to mark ODR-use, which is now a bug).

**Implementation detail:** Extend `drainOneStruct` lambda to skip members where `!isOdrUsed(...)`:

```cpp
auto drainOneStruct = [&](const StructDeclarationNode& struct_decl) {
    StringHandle struct_name = struct_decl.name();
    if (!struct_name.isValid()) return;
    
    for (const auto& member_func : struct_decl.member_functions()) {
        StringHandle member_handle = member_func.getName();
        if (!member_handle.isValid()) continue;
        
        std::optional<bool> is_const_query = std::nullopt;
        if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
            const auto& fn = member_func.function_declaration.as<FunctionDeclarationNode>();
            is_const_query = fn.is_const_member_function();
        }
        
        // NEW: check odr_used first (short-circuit if not marked)
        bool is_const_check = is_const_query.has_value() ? *is_const_query : false;
        if (!lazy_registry.isOdrUsed(struct_name, member_handle, is_const_check)) {
            continue;  // not ODR-used, skip it
        }
        
        // OLD logic: materialize if still needs instantiation
        if (is_const_query.has_value()
                ? lazy_registry.needsInstantiation(struct_name, member_handle, *is_const_query)
                : lazy_registry.needsInstantiationAny(struct_name, member_handle)) {
            auto result = ensureMemberFunctionMaterialized(struct_name, member_handle, is_const_query);
            if (result.has_value()) ++total_materialized;
        }
    }
};
```

---

### Part D: Split `ast_nodes_` into user-written and instantiated lists

**File:** `src/Parser.h`

Currently `ast_nodes_` holds:
- User-written top-level declarations (from initial parse).
- Late-materialized instantiated structs (from `registerLateMaterializedTopLevelNode`).

Split into two, with different dedup semantics:

```cpp
// Existing immutable source-order list:
std::vector<ASTNode> ast_nodes_;  // user-written declarations, dedup'd by source location

// New append-only list for late-materialized instantiations:
std::vector<ASTNode> instantiated_class_template_nodes_;  // dedup'd by raw_pointer()
```

Update accessors:
```cpp
const auto& get_nodes() const { return ast_nodes_; }  // user-written only

const auto& getInstantiatedClassTemplateNodes() const {
    return instantiated_class_template_nodes_;
}

// Move registerLateMaterializedTopLevelNode to push to the instantiated list instead.
void registerLateMaterializedTopLevelNode(const ASTNode& node) {
    // For user-written late nodes (rare), add to ast_nodes_.
    // For instantiated structs, add to instantiated_class_template_nodes_ with dedup.
    if (node.is<StructDeclarationNode>()) {
        // Try to detect if this is an instantiated struct...
        // For now, new callers should call registerInstantiatedClassTemplate directly.
        registerInstantiatedClassTemplate(node);
    } else {
        ast_nodes_.push_back(node);
        enqueuePendingSemanticRoot(node);
    }
}

void registerInstantiatedClassTemplate(const ASTNode& struct_node) {
    FLASH_ASSERT(struct_node.is<StructDeclarationNode>(), "");
    // Dedup by raw_pointer()
    for (const auto& existing : instantiated_class_template_nodes_) {
        if (existing.raw_pointer() == struct_node.raw_pointer()) {
            return;
        }
    }
    instantiated_class_template_nodes_.push_back(struct_node);
    enqueuePendingSemanticRoot(struct_node);  // sema still needs to normalize it
}
```

**Update call sites:**
- In `Parser_Templates_Inst_ClassTemplate.cpp` success returns, call `parser_.registerInstantiatedClassTemplate(instantiated_struct)` (already done in earlier work, just rename the call target).
- Update `SemanticAnalysis::drainLazyMemberRegistry` to walk both lists (already in Part C above).

---

### Part E: Simplify codegen struct-visitor to skip instantiated structs

**File:** `src/IrGenerator_Visitors_Decl.cpp`, the `visit(StructDeclarationNode)` method

Currently, the struct-visitor iterates `parser_.get_nodes()`, which contains both user-written and instantiated structs, and emits every `needsInstantiation=true` member.

New behavior:
1. Only iterate `parser_.get_nodes()` (user-written declarations).
2. For each struct, skip any member where `!lazy_registry.needsInstantiation(...)` (unchanged — still rely on the lazy registry's own tracking for user-written structs).
3. Do NOT visit structs from `parser_.getInstantiatedClassTemplateNodes()` — the drain is responsible for those.

The key insight: user-written structs' members are all emitted (sema never marked them ODR-use, they're emitted by the struct visitor). Instantiated structs' members are only emitted if sema marked them ODR-use (handled by the drain). Codegen visitor doesn't touch instantiated structs at all.

**Why this is safe:**
- The drain runs at end-of-sema, before codegen starts.
- Every ODR-used member of every struct (instantiated or user-written) is already materialized by drain time.
- Codegen struct-visitor is now purely about layout/member iteration for user-written structs; instantiated structs have already been handled by sema.

---

### Part F: Delete `AstToIr::materializeLazyMemberIfNeeded`

**Files:** `src/AstToIr.h`, `src/IrGenerator_Helpers.cpp`

Once the drain handles all ODR-used instantiations, there is no reason for codegen to call this forwarder.

1. Find all callers (grep `materializeLazyMemberIfNeeded`):
   - `IrGenerator_Call_Direct.cpp`: line ~954, 973
   - `IrGenerator_Call_Indirect.cpp`: line ~1089
   - `IrGenerator_MemberAccess.cpp`: line ~3840
   - `IrGenerator_Visitors_Decl.cpp`: line ~1264 (deferred queue seeding)

2. For each call site, verify that:
   - If it's in a deferred-queue seeding context (Part B #4), the `markOdrUsed` was already added in sema, so just remove the call.
   - If it's a remaining codegen-only path (unlikely; Part B should have covered all), convert to an `INTERNAL_ERROR` or remove entirely.

3. Delete the method declaration from `AstToIr.h`.

4. Delete the method definition from `IrGenerator_Helpers.cpp` (along with any remaining audit instrumentation).

---

## Testing Strategy

### Phase 1: Incremental validation after each part

1. After Part A (new registry bits): build, run full suite. Should be no-op semantically (new bits default to false).

2. After Part B (mark ODR-use in sema): this is where the invariant is enforced. Every site that calls `ensureMemberFunctionMaterialized` now marks ODR-use first. Baseline should remain 2201/148 (no regressions).

3. After Part C (drain filters by `odr_used`): drain behavior changes; members without the bit are skipped. However, sema marked everything ODR-used (Part B), so drain still touches the same members. Baseline still 2201/148.

4. After Part D (split lists): purely structural; no semantic change. Baseline still 2201/148.

5. After Part E (codegen stops visiting instantiated structs): codegen behavior changes; instantiated structs are only processed via drain. This is where the risk is: if sema forgot to mark a member ODR-used, codegen won't emit it. **Use an audit guard:** before deleting the forwarder, add a hard-fail in Part E that asserts "codegen should not be visiting instantiated structs". If any test hits this assert, it reveals a missed `markOdrUsed` call in Part B. Fix Part B until the assert never fires, then the delete in Part F is safe.

6. After Part F (delete forwarder): no more codegen materialization fallback. Every member is materialized by sema drain or not at all. Baseline should remain 2201/148.

### Phase 2: Regression test for the architectural guarantee

Add a new test file `tests/test_phase5_slice_g_odr_use_explicit.cpp` or similar that exercises:
- An instantiated struct with a lazy member that is ODR-used (via a call). Should emit.
- An instantiated struct with a lazy member that is NOT ODR-used (not referenced). Should NOT emit, even if the struct is reachable.
- A SFINAE-probed instantiation whose member is never ODR-used. Should NOT emit, even if sema accidentally tried to drain it.

For the third case, we can leverage the existing `test_namespaced_pair_swap_sfinae_ret0.cpp` as a guardrail.

---

## Success Criteria

1. **Build:** `.\build_flashcpp.bat` succeeds with zero warnings/errors.

2. **Test baseline:** `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1` shows **2201 pass / 148 expected-fail / SUCCESS**. (Same as current; the ODR-use changes are non-breaking on existing code.)

3. **Forwarder deleted:** `grep -r "materializeLazyMemberIfNeeded" src/` returns zero matches (the method is gone).

4. **No codegen materializers:** Zero calls to `parser_.instantiateLazyMemberFunction(...)` from any `IrGenerator_*.cpp` file (all deferred to sema).

5. **Audit guard passes:** The hard-fail assert added in Part E (before Part F) never fires across the full test suite.

6. **Regression test passes:** `test_phase5_slice_g_odr_use_explicit.cpp` compiles, links, and returns the expected value.

---

## Detailed Implementation Notes

### LazyMemberInstantiationRegistry structure choice

The registry uses `std::unordered_map<LazyMemberKey, LazyMemberEntry>`. Adding `is_odr_used` to `LazyMemberEntry` is safe and non-invasive.

Alternatively, if the registry stores entries in a structured way, you could add a separate `std::unordered_set<LazyMemberKey>` for ODR-used entries. Either approach works; the former is simpler.

### Const-ness handling

The `std::optional<bool> is_const_member` juggling at call sites is a known papercut. For now, keep the same pattern: callers pass `nullopt` when indifferent (ctor/dtor), and the registry looks up both const and non-const variants if `nullopt` is passed.

However, when marking ODR-use, be precise: if the caller knows const-ness, pass it. This ensures we don't mark the non-const variant as ODR-used if only the const version was called.

### SFINAE-probed instantiations are never marked ODR-used

The key invariant: if a struct is instantiated inside a SFINAE probe (decltype, concepts, enable_if, etc.), no code inside `SemanticAnalysis::tryAnnotate*` or `ConstExpr::Evaluator` will call `markOdrUsed`. Those structs' members stay lazily ill-formed, and the drain skips them.

This is automatic if you follow the pattern in Part B: only sema annotation sites call `markOdrUsed`, and those sites are already guarded by SFINAE context checks at a higher level (e.g., `tryAnnotateConversion` is not called on SFINAE-probed overloads).

### The drain still needs to walk two lists

Because `registerInstantiatedClassTemplate` is called at instantiation time (before the struct is added to the AST walk), the drain must visit both `parser_.get_nodes()` and `parser_.getInstantiatedClassTemplateNodes()` to see all structs. This is fine; Part C handles it.

---

## Rollout Plan

1. **Checkpoint before starting:** commit current state, tag as `phase5-slice-f-complete`.

2. **Implement Part A-D incrementally, with full test run after each part.** This validates that the new infrastructure is wired correctly.

3. **Implement Part E + add the audit guard.** Run full suite; the guard should never fire if Part B was complete.

4. **Implement Part F (delete forwarder).** Final full test run.

5. **Commit with a PR message describing the ODR-use invariant.**

---

## Expected Impact on Downstream Work

- **Phase 6 explicit-deduction cleanup** becomes easier: the deduction system can now assume "if I select this member, sema has already materialized it" without worrying about codegen fallbacks.
- **Future SFINAE / template-argument corner cases** are handled by the same "don't mark ODR-use if not really called" logic, so the architecture is consistent.
- **Constexpr evaluation edge cases** (nested classes, dependent members, etc.) benefit from the same explicit ODR-use signal.

---

## Known Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Miss a call site for `markOdrUsed`, leave member unmaterialized | Use audit guard in Part E (hard-fail before codegen tries to visit) to catch this; the test suite will fail if any member needs materialization. |
| SFINAE-probed struct's member is accidentally marked ODR-used | Code review the sema sites (Part B); assert that SFINAE contexts are already filtered at a higher level (tryAnnotate* not called, evaluator skips probed instantiations). |
| Dedup in `registerInstantiatedClassTemplate` is insufficient | Use `raw_pointer()` (stable for the node's lifetime); add a dedup audit to catch double-pushes before they cause LNK2005s. |
| Drain walks both lists but misses some structs | Use the shared `forEachReachableStructDecl` helper if possible (separate cleanup); for now, verify manually that the two-list walk covers all cases. |

---

## References to Existing Code

- **LazyMemberInstantiationRegistry:** `src/TemplateRegistry_LazyMember.h/cpp`
  - Method `needsInstantiation`, `needsInstantiationAny`, `markInstantiated`.
  - Entry storage: `std::map<LazyMemberKey, LazyMemberEntry>` or similar.

- **SemanticAnalysis::ensureMemberFunctionMaterialized:** `src/SemanticAnalysis.cpp:5384`
  - Existing method that looks up lazy entry, calls `parser_.instantiateLazyMemberFunction`, normalizes, marks instantiated.
  - Will be extended to call `markOdrUsed` first (inside the method or by all callers).

- **SemanticAnalysis::drainLazyMemberRegistry:** `src/SemanticAnalysis.cpp:5415`
  - Current implementation walks `parser_.get_nodes()` and materializes `needsInstantiation=true` members.
  - Will be updated to filter by `odr_used` and walk both node lists.

- **Sema annotation sites:**
  - `tryAnnotateConversion`: `src/SemanticAnalysis.cpp:3800+`
  - `tryAnnotateCallArgConversionsImpl`: `src/SemanticAnalysis.cpp:3400+`
  - `ConstExpr::Evaluator::find_current_struct_member_function_candidate`: `src/ConstExpr_Evaluator.cpp:1200+` (approx.)

- **Codegen struct-visitor:** `src/IrGenerator_Visitors_Decl.cpp:visitStructDeclarationNode`
  - Current method iterates members and emits `needsInstantiation=true`.
  - Will be updated to skip instantiated structs.

- **Current baseline:** 2201 pass / 148 expected-fail (includes new Phase 6 test).

---

## Questions for the Agent

Before starting, clarify:

1. Should `markOdrUsed` be idempotent (calling it twice on the same member is safe)? **Yes**, answer is yes.
2. Should the audit guard in Part E be a hard `INTERNAL_ERROR`, or a log? **Hard error** (ensures no missing marks reach codegen).
3. Is the registry entry storage already a `std::map<LazyMemberKey, ...>` or something else? Check `TemplateRegistry_LazyMember.h`.

---

## Estimated Scope

- **Part A:** 30–50 lines (new methods on registry).
- **Part B:** 50–100 lines (5 sema sites, each adds a 1–2 line call).
- **Part C:** 10–20 lines (add filter in drain loop).
- **Part D:** 40–60 lines (split list, update accessors, minor refactor).
- **Part E:** 20–30 lines (add assert, update struct-visitor to skip instantiated list).
- **Part F:** 10–20 lines (delete method, fix call sites).
- **Tests:** ~40 lines (new regression test).

**Total:** ~200–280 lines of code changes. Low-risk refactor with incremental validation at each step.
