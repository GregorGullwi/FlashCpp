# Refactoring Proposal: Template Argument & Instantiation Pipeline

**Date**: 2026-03-03  
**Status**: All actionable tasks complete — only task 7 (high-risk type unification) remains deferred for a separate PR

---

## Implementation notes (2026-03-04)

| Task | Status | Notes |
|------|--------|-------|
| 4A | ✅ Done | Inline skip guards added to all 3 vulnerable sites |
| 4B | ✅ Done | Site 1 (MemberFunc body reparse) replaced with `registerTypeParamsInScope()` call |
| 6  | ✅ Done | `toTemplateTypeArg(TypeInfo::TemplateArgInfo)` added to `TemplateRegistry_Pattern.h`; local static removed from `ExpressionSubstitutor.cpp` |
| 3  | ✅ Done | `ScopedState<T>` RAII guard added to `ParserScopeGuards.h`; applied to `current_template_param_names_` and `sfinae_type_map_` save/restore in `Parser_Templates_Inst_Deduction.cpp` and `Parser_Templates_Inst_MemberFunc.cpp`. |
| Fix | ✅ Done | Member-function-template non-type body reparse: populated `template_param_substitutions_` and `current_template_param_names_` in `instantiate_member_function_template_core` (MemberFunc.cpp) and the lazy re-parse branch (Lazy.cpp). Test: `tests/member_func_template_nontype_ret0.cpp` |
| 1  | ✅ Done | `Parser::reparse_template_function_body()` extracted in `Parser_Templates_Inst_Deduction.cpp`; both `try_instantiate_template_explicit` and `try_instantiate_single_template` now call it. The old per-path `template_param_substitutions_` loop (21 lines in explicit, 33 lines in deduced) is replaced by a single unified implementation using `toTemplateTypeArg(arg)`. Pack-parameter state and cycle detection remain in callers as planned. |
| 5A | ✅ Done | `toTemplateTypeArg(const TemplateArgument&)` in `TemplateRegistry_Pattern.h` now handles all three kinds (Type, Value, Template). The 20-line inline conversion loop in `try_instantiate_single_template` is replaced by `buildTemplateTypeArgVector()` — a new `static` helper in `Parser_Templates_Inst_Deduction.cpp` that calls `toTemplateTypeArg` for each arg and does the `gTypesByName` index lookup for `Kind::Template`. |
| 2A | ✅ Done | Added `preserve_ref_qualifier` flag (default `false`) to `registerTypeParamsInScope(TemplateTypeArg)` overload in `Parser_Templates_Inst_Deduction.cpp`. Replaced the inline registration loop in `Parser_Templates_Lazy.cpp` and the deferred-body-reparse loop in `Parser_Templates_Inst_ClassTemplate.cpp` (~4830) with `registerTypeParamsInScope(..., true)` calls. |
| 2B | ✅ Done | Added `ASTNode`-based overload of `registerTypeParamsInScope` (accepts `vector<ASTNode>` + optional `sfinae_map*`) to `Parser_Templates_Inst_Deduction.cpp`. Replaced the 16-line SFINAE inner loop in `try_instantiate_template_explicit` (Inst_Deduction) and the 15-line inner loop in `try_instantiate_member_function_template_explicit` (MemberFunc) with single `registerTypeParamsInScope(template_params, template_args, sfinae_scope, &sfinae_type_map_)` calls. The outer_binding sections (which also update `sfinae_type_map_`) remain inline: they use `StringHandle`-keyed `OuterTemplateBinding` data, not parallel `ASTNode/TemplateArgument` vectors. The ClassTemplate deduced-args path (~1893) iterates over an `unordered_map<string,TemplateTypeArg>` — incompatible with the existing helper signatures. Both are deferred (Task 2C). |
| 2C | ✅ Done | Added `static registerOuterBindingInScope(const OuterTemplateBinding&, scope, sfinae_map*)` to `Parser_Templates_Inst_Deduction.cpp`. Replaced the 13-line outer_binding loop in the SFINAE block of `try_instantiate_member_function_template_explicit` (MemberFunc line ~244) and the 11-line outer_binding loop in `instantiate_member_function_template_core` (MemberFunc line ~406) with single `registerOuterBindingInScope(*outer_binding, ...) ` calls. The ClassTemplate deduced-args path (~1893 in `Parser_Templates_Inst_ClassTemplate.cpp`) iterates over an `unordered_map<string, TemplateTypeArg>` rather than parallel vectors, so it remains inline. |
| 8  | ✅ Done | **CTAD + code sharing for `template_param_substitutions_` setup**: (a) Removed all explicit `<std::vector<StringHandle>>` / `<decltype(sfinae_type_map_)>` template arguments from `ScopedState` at 7 sites — CTAD deduces `T` from the constructor argument in C++20. (b) Extracted `Parser::populateTemplateParamSubstitutions` (two overloads: `string_view`/`TemplateTypeArg` for the lazy path; `ASTNode`/`TemplateArgument` for the member-func core path) declared in `Parser.h`, defined in `Parser_Templates_Inst_Deduction.cpp`. The 14-line inline setup loop in `Parser_Templates_Lazy.cpp:174`, the 20-line loop in `Parser_Templates_Inst_MemberFunc.cpp:488`, and the 26-line loop in `reparse_template_function_body` (Inst_Deduction) are each replaced with a single `populateTemplateParamSubstitutions(...)` call. The manual `std::move` save/restore is also replaced with `ScopedState` guards at all three sites. |
| 9  | ✅ Done | **Task 3 continuation — remaining `current_template_param_names_` / `parsing_template_body_` save/restore sites**: `Parser_Templates_Function.cpp` (`parse_member_function_template`): 1 save + 13 restore-before-return patterns → `ScopedState`; `Parser_Templates_Variable.cpp` (`parse_member_template_alias`): 2 saves + 7 restore-before-return patterns (for both `current_template_param_names_` and `parsing_template_body_`) → 2 `ScopedState` guards; `Parser_Templates_Class.cpp`: 3 patterns (`is_partial_specialization` block, partial-body block using `ScopeGuard`+lambda, struct-body block using `ScopeGuard`+lambda) → all converted to `ScopedState`. Manual `std::move` + restore lines eliminated: ~30 lines total. |
| 10 | ✅ Done | **Task 3 continuation (round 2) — 2 remaining unconditional save/restore sites**: (a) `Parser_Templates_Lazy.cpp:152` (`bool saved_parsing_template_body = parsing_template_body_; parsing_template_body_ = false;`) → `ScopedState guard_ptb(parsing_template_body_)` + set to false; restore line removed. (b) `Parser_Templates_Inst_ClassTemplate.cpp:4275–4280` (4-field unconditional block: `saved_template_body`, `saved_template_class`, `saved_param_names`, `saved_delayed_bodies`) → 4 `ScopedState` guards (`guard_template_body`, `guard_template_class`, `guard_param_names`, `guard_delayed_bodies`); 4 manual restore lines + stale comment eliminated. |
| 11 | ✅ Done | **Task 3 continuation (round 3) — last conditional save/restore site in `Class.cpp:2378`**: Extended `ScopedState` with a two-parameter constructor `ScopedState(T& field, bool active)` — when `active=false` the field is left untouched and the destructor is a no-op; `std::optional<T>` used for `saved_state_` (no default `T{}` constructed when inactive, `bool active_` member eliminated). Converted the `!delayed.template_param_names.empty()` conditional save/restore of `current_template_param_names_` and `parsing_template_body_` in `Class.cpp` to `ScopedState(field, has_template_params)`. |
| 12 | ✅ Done | **StringHandle migration for `param_names` and `TemplateParamSubstitution::param_name`**: (a) Changed all `param_names` build loops in `Deduction.cpp`, `MemberFunc.cpp`, `Lazy.cpp`, `ClassTemplate.cpp` from `vector<string_view>` to `vector<StringHandle>` using `nameHandle()` instead of `name()`. (b) Updated `registerTypeParamsInScope` (TemplateTypeArg + TemplateArgument overloads), `registerOuterBindingInScope` (uses `outer_binding.param_names[i]` directly), `populateTemplateParamSubstitutions` overload 1, and `Parser.h` declaration to use `StringHandle`. Removed all `getOrInternStringHandle(name)` / `getStringView(handle)` round-trips at these sites. (c) Changed `TemplateParamSubstitution::param_name` from `string_view` to `StringHandle`; updated the 4 assignment sites in `Deduction.cpp` and `ClassTemplate.cpp` to use `nameHandle()`; updated the 2 comparison sites in `Substitution.cpp` (`dep_name`/`type_name` variables now `StringHandle`). (d) Added `ScopedState` for `current_function_` in `reparse_template_function_body`. (e) Removed 6 redundant `.clear()` calls after `ScopedState` construction (gemini-code-assist review). |
| 5B | ✅ Done | **Eliminate `template_args_as_type_args` from `try_instantiate_single_template`**: (a) Added `TemplateArgument::toString()` method (delegates to `toTemplateTypeArg(*this).toString()`). (b) Moved `buildTemplateTypeArgVector` from `static` in `Parser_Templates_Inst_Deduction.cpp` to `inline` in `TemplateRegistry_Pattern.h` (alongside `toTemplateTypeArg`). (c) Added thin shim overloads accepting `vector<TemplateArgument>` for: `TemplateRegistry::lookupSpecialization` (Registry.h), `evaluateConstraint` (Lazy.cpp), `Parser::substitute_template_parameter` (QualLookup.cpp + Parser.h), `Parser::try_instantiate_class_template` (ClassTemplate.cpp + Parser.h), `Parser::get_instantiated_class_name` (Substitution.cpp + Parser.h). `registerTypeParamsInScope` already had a `TemplateArgument` overload. (d) Updated all 17 references to `template_args_as_type_args` in `try_instantiate_single_template` to use `template_args` directly. The intermediate vector is eliminated; each downstream function now converts internally via `buildTemplateTypeArgVector` when needed. |
| 7  | ⏳ Deferred | Unify `TemplateArgument` / `TemplateTypeArg` — high risk; separate PR. Preconditions (tasks 1–3) are now met. |

---

## Motivation

While implementing non-type template parameter deduction (deducing `N=5` from a call
`getSize(arr)` where `arr : Array<int,5>`), two classes of problems became apparent:

1. **A silent data-loss bug**: `TemplateArgument::Kind::Value` entries were silently
   dropped when converting the deduced argument vector to `TemplateTypeArg` format for
   downstream substitution.  This caused the deduced value to never reach the body
   re-parsing stage.

2. **A missing setup step**: `try_instantiate_single_template` did not populate
   `current_template_param_names_` before re-parsing the function body, so the symbol
   table never returned `TemplateParameterReferenceNode` for `N`.  The same setup
   exists in `try_instantiate_template_explicit` — the duplication caused a divergence
   that was hard to spot.

Both bugs are fixed, but they are symptoms of a deeper structural issue: the
instantiation pipeline maintains **three parallel representations** of the same
template arguments, **two near-identical body-parsing code paths** that slowly drift
apart, and a collection of **repeated boilerplate loops** scattered across eight or
more files.  This document catalogues all of those patterns and proposes a phased
clean-up.

---

## Current state (as of 2026-03-03)

### A. Three parallel argument vectors (inside `try_instantiate_single_template`)

The same template arguments are held simultaneously in three different forms:

| Vector | Type | Purpose |
|--------|------|---------|
| `template_args` | `std::vector<TemplateArgument>` | Deduction result; canonical during deduction |
| `deduced_type_args` | `std::vector<Type>` | Side-channel for types extracted from template-template params |
| `template_args_as_type_args` | `std::vector<TemplateTypeArg>` | Converted form consumed by substitution helpers and `try_instantiate_class_template` |

Converting between these forms is error-prone.  The `Kind::Value` case was missing from
the `template_args → template_args_as_type_args` loop before being caught.

**Design suggestion** (from code review): it might make sense to unify the unresolved and
resolved members into a single array entry:

```
{ Type, TypeIndex, TVariant<int64_t/StringHandle/...> }
```

where the `TVariant` slot handles non-type values and the `Type`/`TypeIndex` pair handles
type arguments.  This would eliminate the fan-out into three vectors while keeping the
same information density.

### B. Two near-identical function-body re-parsing blocks

`try_instantiate_template_explicit` (~line 385–560) and `try_instantiate_single_template`
(~line 1595–1820) both:

* restore the lexer to `template_body_position`
* enter a function scope and register parameters
* clear and populate `template_param_substitutions_`
* clear and populate `current_template_param_names_`
* call `parse_block()`
* call `substituteTemplateParameters()` on the result
* call `new_func_ref.set_definition()`

They diverge in small but important ways (the `current_template_param_names_` setup was
missing from the deduced path), and any future change must be applied to both.

### C. Two type representations for the same concept

`TemplateArgument` (`TemplateRegistry_Pattern.h`) and `TemplateTypeArg`
(`TemplateRegistry_Types.h`) represent a single resolved template argument at different
pipeline stages.  Both are fat structs; `TemplateTypeArg` uses `is_value: bool` while
`TemplateArgument` uses a `Kind` enum.  There are also two *separate* `toTemplateTypeArg`
conversion functions:

- `TemplateRegistry_Pattern.h`: `toTemplateTypeArg(const TemplateArgument&)` — the
  canonical conversion used across the parser.
- `ExpressionSubstitutor.cpp` line 7: `static toTemplateTypeArg(const TypeInfo::TemplateArgInfo&)` —
  a **local duplicate** that converts from the `TypeInfo`-embedded form.  This function
  duplicates the field-copy logic but is not reachable from `TemplateRegistry_Pattern.h`
  because `TypeInfo::TemplateArgInfo` is defined in `AstNodeTypes_DeclNodes.h`.

### D. `current_template_param_names_` save/clear/restore scattered across 9 files

The three-line pattern
```cpp
auto saved = std::move(current_template_param_names_);
current_template_param_names_.clear();
/* ... populate ... */
current_template_param_names_ = std::move(saved);
```
appears in:

| File | Occurrences |
|------|-------------|
| `Parser_Templates_Class.cpp` | ~22 (many are standalone `clear()` without restore) |
| `Parser_Templates_Function.cpp` | ~13 |
| `Parser_Templates_Variable.cpp` | 7 |
| `Parser_Templates_Params.cpp` | 4 |
| `Parser_Templates_Inst_Deduction.cpp` | 3 |
| `Parser_Templates_Inst_MemberFunc.cpp` | 2 |
| `Parser_Decl_FunctionOrVar.cpp` | 2 |
| `Parser_Templates_Inst_ClassTemplate.cpp` | 1 |

A RAII guard (analogous to the existing `TemplateParameterScope`) could wrap the
save/restore into a single object and eliminate a whole class of "restore on all exit
paths" bugs.

### E. `param_names + template_args → gTypeInfo/gTypesByName` loop duplicated in 8+ places

The pattern
```cpp
for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
    auto& type_info = gTypeInfo.emplace_back(
        StringTable::getOrInternStringHandle(param_names[i]),
        template_args[i].type_value,
        gTypeInfo.size(),
        getTypeSizeFromTemplateArgument(template_args[i]));
    gTypesByName.emplace(type_info.name(), &type_info);
    template_scope.addParameter(&type_info);
}
```
appears with minor variations in:

| File | Lines |
|------|-------|
| `Parser_Templates_Inst_Deduction.cpp` | ~400, ~1204, ~1712 |
| `Parser_Templates_Inst_MemberFunc.cpp` | ~243, ~428 |
| `Parser_Templates_Inst_ClassTemplate.cpp` | ~1893, ~4826 |
| `Parser_Templates_Lazy.cpp` | ~147 |
| `Parser_Expr_PrimaryExpr.cpp` | ~1359 |
| `TemplateRegistry_Lazy.cpp` | ~914, ~953, ~1002, ~1214, ~1238, ~1489 |

The variations include:
- Some use `TemplateArgument`, some use `TemplateTypeArg`, some use
  `TypeInfo::TemplateArgInfo` as the source vector.
- Some guard value params (`arg.kind == Kind::Value`), most do not (Bug 4 below).
- Some use `get_type_size_bits()` instead of `getTypeSizeFromTemplateArgument()`.
- `getTypeSizeFromTemplateArgument` itself is `static` in `Parser_Core.cpp` and cannot
  be shared with `TemplateRegistry_Lazy.cpp` or `ExpressionSubstitutor.cpp`.

All of these should call a single `Parser::registerTemplateParamsAsTypeInfo()` helper.

### F. `template_param_substitutions_` save/clear/restore — 2 files

Only `Parser_Templates_Inst_Deduction.cpp` (7 occurrences) and
`Parser_Templates_Inst_ClassTemplate.cpp` (1 occurrence) save/restore
`template_param_substitutions_`.  This is much less scattered than
`current_template_param_names_` but still benefits from the body-helper (proposal 1).

---

## Proposed changes

### 1 — Extract `Parser::reparse_template_function_body()`

```cpp
// Takes ownership of nothing; all state is local.  Sets new_func's definition.
void Parser::reparse_template_function_body(
    FunctionDeclarationNode&         new_func,
    const FunctionDeclarationNode&   template_decl,
    const std::vector<ASTNode>&      template_params,
    const std::vector<TemplateArgument>& template_args);
```

Both `try_instantiate_template_explicit` and `try_instantiate_single_template` call
this instead of duplicating the ~80-line block.  This eliminates the class of bugs where
one path drifts from the other (including the `current_template_param_names_` divergence
that caused the `N` bug).

**Performance note** (from code review): template parameter counts are almost always small
(1–4), so the `std::vector<TemplateArgument>` parameter could be `InlineVector<TemplateArgument, 4>`
to avoid heap allocation in the hot instantiation path.

**Touches**: 2 call sites in `Parser_Templates_Inst_Deduction.cpp`.  
**Risk**: low — pure extraction, no logic change.

---

### 2 — Extract `Parser::registerTemplateParamsAsTypeInfo()`

```cpp
// Registers type params as TypeInfo entries in gTypesByName (for the duration of a
// template scope) and value params in template_param_substitutions_.
// Skips Kind::Value args for TypeInfo (fixes the Invalid-type registration bug).
void Parser::registerTemplateParamsAsTypeInfo(
    const std::vector<std::string_view>&  param_names,
    const std::vector<TemplateArgument>&  template_args,
    FlashCpp::TemplateParameterScope&     scope);
```

Replaces the ~12 copies of the `param_names + template_args → gTypeInfo` loop.  All
three source-vector types (`TemplateArgument`, `TemplateTypeArg`,
`TypeInfo::TemplateArgInfo`) can be handled by overloads or by converting to
`TemplateArgument` first.  Moving `getTypeSizeFromTemplateArgument` out of
`Parser_Core.cpp` into a shared header (e.g., `TemplateRegistry_Types.h`) would allow
all sites to use the same size computation.

**Performance note** (from code review): since template parameter lists are typically very
short, `InlineVector<std::string_view, 4>` for `param_names` would keep these on the stack.

**Touches**: `Parser_Templates_Inst_Deduction.cpp`, `Parser_Templates_Inst_MemberFunc.cpp`,
`Parser_Templates_Inst_ClassTemplate.cpp`, `Parser_Templates_Lazy.cpp`,
`TemplateRegistry_Lazy.cpp`, `Parser_Expr_PrimaryExpr.cpp`.  
**Risk**: medium — many sites, but each is a straightforward mechanical substitution.

---

### 3 — Introduce a `ScopedTemplateParamNames` RAII guard

A guard that saves and restores `current_template_param_names_` around template body re-parsing.

**Design note** (from code review): coupling population logic to the guard constructor makes it
harder to reuse across sites where the sources differ.  A more flexible form separates the RAII
mechanism from the population:

```cpp
// Decoupled form — saves on construction, restores on destruction.
// The caller is responsible for clearing and populating the vector.
class ScopedState {
public:
    explicit ScopedState(std::vector<StringHandle>& field)
        : field_ref_(field), saved_state_(std::move(field)) {}
    ~ScopedState() { field_ref_ = std::move(saved_state_); }
    ScopedState(const ScopedState&) = delete;
    ScopedState& operator=(const ScopedState&) = delete;
private:
    std::vector<StringHandle>& field_ref_;
    std::vector<StringHandle>  saved_state_;
};

// Usage:
{
    ScopedState guard(current_template_param_names_);
    current_template_param_names_.clear();
    // ... custom population logic ...
} // old state restored here
```

**Performance note** (from code review): since template parameter lists are almost always
small (typically 1–4 elements), using `InlineVector<StringHandle, 4>` instead of
`std::vector<StringHandle>` would eliminate heap allocation in the common case.  This applies
to both `current_template_param_names_` itself and the `saved_state_` member inside the guard.

Replaces the ~50 manual save/clear/restore occurrences of `current_template_param_names_`
spread across 9 files.  Since it follows the same RAII pattern as
`TemplateParameterScope` it fits naturally in `ParserScopeGuards.h`.

**Touches**: 9 files (see table in section D).  
**Risk**: low per site, medium overall due to volume.

---

### 4 — Skip TypeInfo registration for value params at all sites

`registerTypeParamsInScope` (added in `Parser_Templates_Inst_Deduction.cpp:16-71` by
PR #839) already skips `Kind::Value` and `Kind::Template` entries for the three paths
inside that file.  However, the helper is `static` (file-local), so it cannot be called
from the three remaining vulnerable sites:

| File | Line(s) | Source type | Currently skips Value? |
|------|---------|-------------|----------------------|
| `Parser_Templates_Inst_MemberFunc.cpp` | ~424-431 | `vector<TemplateArgument>` | **No** |
| `Parser_Templates_Inst_MemberFunc.cpp` | ~240-249 | `vector<TemplateArgument>` | **No** |
| `Parser_Templates_Lazy.cpp` | ~143-154 | `vector<TemplateTypeArg>` (via `LazyMemberFunctionInfo`) | **No** |

These sites are latent today because non-type template parameters are not yet supported
in the member function template deduction path (line 61-63 returns `std::nullopt`).
Once non-type params are extended to member templates, all three will poison
`gTypesByName` with `Type::Invalid` entries, exactly as `try_instantiate_single_template`
did before this PR.

#### Option A — inline skip (minimal, immediate)

Add `Kind::Value`/`Kind::Template` (or `is_value`/`is_template_template_arg`) guards
inline at each of the three sites.  ~3 lines per site, no cross-file changes.

#### Option B — promote `registerTypeParamsInScope` to a shared helper (proper fix)

1. Move `getTypeSizeFromTemplateArgument` from `static` in `Parser_Core.cpp` to a
   shared location (e.g., a new free function in `TemplateRegistry_Types.h`, or a
   `Parser::` member).

2. Move both overloads of `registerTypeParamsInScope` (the `TemplateArgument` overload
   and the `TemplateTypeArg` overload) from `static` in
   `Parser_Templates_Inst_Deduction.cpp` to `Parser::` member functions (declared in
   `Parser.h`), or to free functions in a shared header that takes the required globals
   as parameters.

3. Replace all six existing call sites in `Parser_Templates_Inst_Deduction.cpp` (already
   calling the `static` version) with calls to the new shared version — no behaviour
   change, just linkage.

4. Replace the three vulnerable sites listed above with calls to the shared helper:

   **`Parser_Templates_Inst_MemberFunc.cpp:424-431`** (body reparse in
   `instantiate_member_function_template_core`):
   ```cpp
   // Before:
   for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
       Type concrete_type = template_args[i].type_value;
       auto& type_info = gTypeInfo.emplace_back(...);
       gTypesByName.emplace(type_info.name(), &type_info);
       template_scope.addParameter(&type_info);
   }
   // After:
   registerTypeParamsInScope(param_names, template_args, template_scope);
   ```

   **`Parser_Templates_Inst_MemberFunc.cpp:240-249`** (SFINAE trailing-return-type in
   `try_instantiate_member_function_template_explicit`):
   ```cpp
   // Before:  loops over template_params[i] / template_args[i] with no skip
   // After:   same call, but this loop also updates sfinae_type_map_, so either:
   //   (a) split into registerTypeParamsInScope() + a separate sfinae_type_map_ loop, or
   //   (b) add an optional sfinae_type_map* parameter to the helper.
   ```

   **`Parser_Templates_Lazy.cpp:143-154`** (lazy member function body reparse):
   ```cpp
   // Before:
   for (size_t i = 0; i < param_names.size() && i < lazy_info.template_args.size(); ++i) {
       Type concrete_type = lazy_info.template_args[i].base_type;
       auto& type_info = gTypeInfo.emplace_back(...);
       gTypesByName.emplace(type_info.name(), &type_info);
       template_scope.addParameter(&type_info);
   }
   // After (TemplateTypeArg overload):
   registerTypeParamsInScope(param_names, lazy_info.template_args, template_scope);
   ```
   Note: the lazy path currently also copies `reference_qualifier_` from the
   `TemplateTypeArg` onto the `TypeInfo`; the `TemplateTypeArg` overload of
   `registerTypeParamsInScope` does not do this.  Either add it to the overload
   (behind a flag, like `preserve_ref_qualifier` on the `TemplateArgument` overload)
   or handle it after the call.

5. The SFINAE trailing-return-type loop in `try_instantiate_template_explicit`
   (`Parser_Templates_Inst_Deduction.cpp:332-349`) also remains inline because it
   updates `sfinae_type_map_`.  The same `sfinae_type_map*` parameter from step 4b
   would unify this site too.

**Touches**: `Parser.h` (declaration), `Parser_Core.cpp` or `TemplateRegistry_Types.h`
(move `getTypeSizeFromTemplateArgument`), `Parser_Templates_Inst_Deduction.cpp` (remove
`static`, update calls), `Parser_Templates_Inst_MemberFunc.cpp` (2 sites),
`Parser_Templates_Lazy.cpp` (1 site).  
**Risk**: low — the helper already exists and is tested; the change is purely making it
reachable from other translation units.

**Recommendation**: Do Option A immediately (3 lines each, prevents the latent bug from
activating when non-type member template support lands).  Follow up with Option B as
part of proposal 2, since `registerTypeParamsInScope` is essentially the
`registerTemplateParamsAsTypeInfo` helper proposed there.

---

### 5 — Eliminate `template_args_as_type_args` in `try_instantiate_single_template`

After the deduction pass `template_args` already holds all type + value information.
Downstream helpers that accept `std::vector<TemplateTypeArg>` can receive thin shim
overloads accepting `std::vector<TemplateArgument>`, or the signatures can be widened.
The goal is a **single authoritative vector** that is not duplicated.

Key helpers to update:
- `try_instantiate_class_template(name, vector<TemplateTypeArg>)`
- `substitute_template_parameter(type, params, vector<TemplateTypeArg>)`
- `lookupSpecialization(name, vector<TemplateTypeArg>)`

The canonical conversion helpers (`toTemplateArgument` / `toTemplateTypeArg` in
`TemplateRegistry_Pattern.h`) already handle the full round-trip; the work is wiring up
the call sites.

**Touches**: `Parser_Templates_Inst_Deduction.cpp` primarily, plus any helper signatures.  
**Risk**: medium.

---

### 6 — Consolidate the two `toTemplateTypeArg` functions

`TemplateRegistry_Pattern.h:108` converts `TemplateArgument → TemplateTypeArg`.  
`ExpressionSubstitutor.cpp:7` converts `TypeInfo::TemplateArgInfo → TemplateTypeArg`.

Both copy the same set of fields.  Add an overload (or a conversion constructor on
`TemplateTypeArg`) that accepts `TypeInfo::TemplateArgInfo` directly, so the local
`static` function in `ExpressionSubstitutor.cpp` can be removed and the canonical
header is the only place that knows about the conversion.

**Touches**: `ExpressionSubstitutor.cpp`, `TemplateRegistry_Types.h` or
`TemplateRegistry_Pattern.h`.  
**Risk**: very low — pure code sharing.

---

### 7 — Unify `TemplateArgument` and `TemplateTypeArg`

Long-term, having two types for the same concept creates maintenance burden.  The
recommended direction is to **extend `TemplateTypeArg`** to carry a `Kind` enum (or an
explicit `is_template_template` flag) and retire `TemplateArgument`, or alternatively
to **extend `TemplateArgument`** to carry the full `TypeSpecifierNode` info that
`TemplateTypeArg` holds (it already has `type_specifier` as an optional field).

A `std::variant<TypeArg, ValueArg, TemplateTemplateArg>` was considered but rejected:
the fat-struct approach is already established in the codebase and avoids the syntactic
noise of `std::visit` at every use site.

**Touches**: most template-related files (~200 occurrences of each type in the codebase).  
**Risk**: high.  Should be done as a separate PR after (1)–(3) are merged and tested.

---

## Priority order

| # | Change | Effort | Risk | Benefit | Status |
|---|--------|--------|------|---------|--------|
| 4A | Inline skip at 3 remaining sites | XS | Very low | Fixes latent Invalid-type bug | ✅ Done |
| 4B | Promote `registerTypeParamsInScope` to shared | S | Low | Eliminates the duplication that caused the bug | ✅ Done |
| 6 | Consolidate `toTemplateTypeArg` | XS | Very low | Removes duplicate conversion | ✅ Done |
| 1 | Extract `reparse_template_function_body()` | S | Low | Eliminates body-parse divergence class | ✅ Done |
| 3 | `ScopedTemplateParamNames` RAII guard | M | Low | Removes ~50 manual save/restore sites | ✅ Done |
| 2 | Extract `registerTemplateParamsAsTypeInfo()` | M | Medium | Removes ~12 loop duplicates | ✅ Done |
| 5 | Eliminate `template_args_as_type_args` | M | Medium | Single authoritative arg vector | ✅ Done |
| 7 | Unify `TemplateArgument` / `TemplateTypeArg` | L | High | One type, no conversions | ⏳ Deferred |

All items except task 7 are complete.  Task 7 (unifying `TemplateArgument` and
`TemplateTypeArg`) is deferred to a separate PR due to its high risk and wide
scope (~200 occurrences across the codebase).  Its preconditions (tasks 1–3) are
now met, and the thin shim overloads added in task 5B provide a clean migration
path: each shim can be replaced with native `TemplateArgument` support incrementally.

### Known issue: `resolve_dependent_member_alias` passes full function template args to class template instantiation

**Status**: ⏳ Investigate — pre-existing (not introduced by task 5B)

In `try_instantiate_single_template`, the `resolve_dependent_member_alias` lambda
(~line 1447) passes the function template's full `template_args` vector to
`try_instantiate_class_template` and `get_instantiated_class_name` when attempting
to instantiate a base class template (lines 1518-1520).  This is correct when the
function template and the base class template share the same parameters (the common
case for patterns like `template<typename T> typename Helper<T>::type foo(T x)`),
but would be wrong when they differ.

**Example that would be incorrect** (not yet tested):
```cpp
template<typename T>
struct Helper { using type = T; };

// Two template params, but Helper only uses the second
template<typename T, typename U>
typename Helper<U>::type foo(T x, U y) { return y; }

int main() { return foo(3.14, 0); }
```
Here `template_args = [double, int]` but `Helper` expects one arg (`[int]`).
The full `[double, int]` vector gets passed to `try_instantiate_class_template("Helper", ...)`.

**Action items**:
1. Write a test exercising this pattern and verify whether it passes or fails.
2. If it fails, fix `resolve_dependent_member_alias` to extract only the args
   relevant to the base class template (e.g. by matching the substituted
   `base_part` string against the class template's parameter list).

---

## Files most affected

| File | Proposals |
|------|-----------|
| `src/Parser_Templates_Inst_Deduction.cpp` | 1, 2, 4B, 5 |
| `src/Parser_Templates_Class.cpp` | 3 |
| `src/Parser_Templates_Function.cpp` | 3 |
| `src/Parser_Templates_Variable.cpp` | 3 |
| `src/Parser_Templates_Inst_MemberFunc.cpp` | 2, 4A/4B |
| `src/Parser_Templates_Inst_ClassTemplate.cpp` | 2, 4A |
| `src/Parser_Templates_Lazy.cpp` | 2, 4A/4B |
| `src/TemplateRegistry_Lazy.cpp` | 2, 4A |
| `src/ExpressionSubstitutor.cpp` | 6 |
| `src/TemplateRegistry_Pattern.h` / `TemplateRegistry_Types.h` | 6, 7 |
| `src/ParserScopeGuards.h` | 3 |
| `src/Parser.h` | 4B (declaration of shared helper) |
| `src/Parser_Core.cpp` | 2, 4B (move `getTypeSizeFromTemplateArgument` to shared header) |
