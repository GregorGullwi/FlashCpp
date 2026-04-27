# FlashCpp Known Issues

## Issue 1: `std::rel_ops` operators incorrectly instantiated via unqualified template-registry lookup

### Summary
`std::rel_ops::operator<=<pair<int,float>>` (and the analogous `!=`, `>`, `>=` overloads) gets
instantiated — and added to codegen — even though per C++20 [basic.lookup.argdep], `std::rel_ops`
is **not** an associated namespace of `std::pair<int,float>`.

### Affected file
`tests/std/test_std_utility.cpp` (and any translation unit that includes `<utility>` and uses a
comparison operator whose operand type lives in namespace `std`).

### Root cause — two compounding bugs

---

#### Bug A — Phase 1 of `tryInstantiateOperatorTemplate` bypasses namespace scoping

**Location:** `src/Parser_Expr_BinaryPrecedence.cpp` ~line 197–202

```cpp
// Phase 1: try the template registry (covers most non-ADL templates)
ScopedParserInstantiationContext guard_instantiation_mode_phase1(
    *this, TemplateInstantiationMode::SfinaeProbe, StringHandle{});
if (std::optional<ASTNode> instantiated =
        try_instantiate_template(op_name, arg_types);  // ← broken lookup
    instantiated.has_value()) { ... }
```

`try_instantiate_template("operator<=", ...)` calls
`gTemplateRegistry.lookupAllTemplates("operator<=")`.  Because
`forEachQualifiedName` (in `src/TemplateRegistry_Registry.h` ~line 942) registers every
template under **both** the simple name *and* the fully-qualified name:

```cpp
void forEachQualifiedName(QualifiedIdentifier qi, Fn&& fn) {
    std::string_view simple = StringTable::getStringView(qi.identifier_handle);
    fn(simple);           // ← always registers under "operator<="
    if (qi.hasNamespace()) {
        ...
        fn(qualified_name);  // also registers under "std::rel_ops::operator<="
    }
}
```

…the call to `lookupAllTemplates("operator<=")` returns **every** `operator<=` template
ever parsed, regardless of namespace.  This includes `std::rel_ops::operator<=<_Tp>`.

Template argument deduction of `_Tp = pair<int,float>` succeeds (the function takes
`const _Tp& x, const _Tp& y`), so `try_instantiate_single_template` is invoked.

**C++ standard rule violated:**  
C++20 [over.match.oper]/3 + [basic.lookup.argdep]/2–3 — operator overload candidates
are gathered by (a) ordinary unqualified lookup in the current scope and (b) ADL in the
*associated* namespaces of the argument types.  For `std::pair<int,float>` the only
associated namespace is `std`; `std::rel_ops` is a distinct nested namespace and is
**not** associated unless a `using namespace std::rel_ops;` is in effect.

Phase 1's use of `gTemplateRegistry.lookupAllTemplates(unqualified_name)` is effectively
a "lookup everywhere, ignoring all namespace scoping", which produces spurious candidates.

---

#### Bug B — `try_instantiate_single_template` unconditionally registers successful instantiations for codegen, even in `SfinaeProbe` mode

**Location:** `src/Parser_Templates_Inst_Deduction.cpp` ~line 2402–2412

```cpp
// Register the instantiation
gTemplateRegistry.registerInstantiation(key, new_func_node);

// Add to symbol table at GLOBAL scope
gSymbolTable.insertGlobal(mangled_token.value(), new_func_node);

// Add to top-level AST so it gets visited by the code generator
registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);   // ← always called
```

`registerAndNormalizeLateMaterializedTopLevelNode` pushes the node into `ast_nodes_`
with `ast_node_is_instantiated_[i] = 1`.  There is **no guard** for
`TemplateInstantiationMode::SfinaeProbe`.

Because the caller in Phase 1 (Bug A) wraps the call in `SfinaeProbe` mode, the intent
is that the instantiation result is only used for candidate scoring — not for code
generation.  But once the node is in `ast_nodes_`, the main codegen loop
(`src/FlashCppMain.cpp` ~line 504) will visit it unconditionally:

```cpp
// Re-evaluates ast.size() each iteration so late-added nodes are also visited.
for (size_t node_index = 0; node_index < ast.size(); ++node_index) {
    converter.visit(ast[node_index]);
}
```

So even though `std::rel_ops::operator<=<pair<int,float>>` **loses** the candidate
ranking to the more-specific `std::operator<=<_T1,_T2>` from `<bits/stl_pair.h>`, it
has already been committed to codegen as a side effect of the SFINAE probe.

---

### Complete instantiation chain (for `pair<int,float>` as operands of `<=`)

1. `parse_expression` encounters a `<=` binary operator whose LHS/RHS are both
   `pair<int,float>` (this happens when the body of `std::operator<` for pair is
   instantiated, or when user code directly compares two pairs).

2. `tryInstantiateOperatorTemplate("<=", pair<int,float>, pair<int,float>)` is called
   (lines 721 or 772 of `Parser_Expr_BinaryPrecedence.cpp`).

3. **Phase 1** calls `try_instantiate_template("operator<=", {pair,pair})`.

4. `try_instantiate_template` → `gTemplateRegistry.lookupAllTemplates("operator<=")`
   → returns the vector containing *both*:
   - `std::operator<=<_T1,_T2>` from `<bits/stl_pair.h>` (registered under "operator<=")
   - `std::rel_ops::operator<=<_Tp>` from `<bits/stl_relops.h>` (also registered under
     "operator<=" by `forEachQualifiedName`)

5. `try_instantiate_template` calls `try_instantiate_single_template` for each.
   For `std::rel_ops::operator<=<_Tp>`:
   - Deduction: `_Tp = pair<int,float>` — succeeds.
   - No requires-clause, no concept constraint — passes.
   - Body reparsed: `return !(__y < __x)` with `__y, __x : pair<int,float>&`.
   - Instantiated `FunctionDeclarationNode` created.
   - **Line 2412**: `registerAndNormalizeLateMaterializedTopLevelNode(new_func_node)`
     → node appended to `ast_nodes_`, tagged `is_instantiated = 1`.
   - Returns the new node.

6. Back in `try_instantiate_template` (SFINAE-mode), `std::rel_ops::operator<=` is
   collected as a candidate with specificity 0 (bare `_Tp` parameter).
   `std::operator<=` for pair has specificity ≥ 2 (concrete instantiated struct params).
   The pair-specific overload wins; Phase 1 returns *that* one.

7. `std::rel_ops::operator<=<pair<int,float>>` is **never selected** as the operator
   overload, but is already in `ast_nodes_` → codegen loop visits it →
   machine code emitted for it → linker symbol created.

---

### Suggested fixes

**Fix A (correct layer — template registry / Phase 1):**  
Remove Phase 1's template-registry-based lookup entirely.  Phase 2 already performs
correct ordinary-lookup + ADL-only lookup via `gSymbolTable.lookup_all(op_name)` and
`gSymbolTable.lookup_adl_only(op_name, arg_types)`.  The symbol table respects namespace
scoping, so `std::rel_ops::operator<=` is only reachable there if a `using namespace
std::rel_ops;` is in effect — the correct C++ behaviour.

**Fix B (correct layer — SFINAE probe registration):**  
In `try_instantiate_single_template` (`src/Parser_Templates_Inst_Deduction.cpp`
~line 2411–2412), guard the `registerAndNormalizeLateMaterializedTopLevelNode` call with
a check for `SfinaeProbe` mode:

```cpp
// Only commit to codegen when this is a real (non-probe) instantiation.
if (template_instantiation_mode_ != TemplateInstantiationMode::SfinaeProbe) {
    registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);
}
```

The `gTemplateRegistry.registerInstantiation` caching call (line 2403) should remain
unconditional so that the result is reused on the next lookup without re-instantiating.
When the selected winner is later confirmed and code for it is actually needed, a
subsequent non-probe call will reach `registerAndNormalizeLateMaterializedTopLevelNode`.

**Fix C (defense-in-depth — `forEachQualifiedName` for operator templates):**  
Consider NOT registering operator function templates under the bare unqualified name when
the enclosing namespace is neither global nor `std` (top-level).  Sub-namespaces like
`std::rel_ops` intentionally confine their operators; registering them under "operator<="
defeats that confinement.

---

### Standard references
- C++20 [over.match.oper]/3: candidate set for built-in and user-defined operators
- C++20 [basic.lookup.argdep]/2–3: associated namespaces for class types
- C++20 [temp.deduct]: SFINAE — substitution failure is not an error, but also must not
  have observable side effects (codegen registration is an observable side effect)
