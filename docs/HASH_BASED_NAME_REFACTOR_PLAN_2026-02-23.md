# Plan: Replace String-Based Name Checks with Hash-Based Lookups

**Date:** 2026-02-23  
**Author:** Copilot  
**Context:** Follow-up to PR "Fix namespace-qualified template struct false-match in codegen struct search"

---

## Background

FlashCpp's template instantiation system uses three naming conventions that are currently detected
through `std::string_view::find` substring searches throughout codegen and the parser:

| Convention | Example | Meaning |
|---|---|---|
| `_pattern_` suffix | `"array_pattern_N_T"` | Partial specialization pattern struct |
| `_unknown` suffix | `"is_reference_int_unknown"` | Incomplete instantiation (unresolved type arg) |
| `$` separator | `"array$bb125dafdd4970ce"` | Full hash-based template instantiation (new scheme) |

These substring checks appear in **30+ call sites** across 6 source files:

```
src/CodeGen_Functions.cpp    – 5 sites
src/CodeGen_Visitors.cpp     – 6 sites
src/CodeGen_Expressions.cpp  – 1 site
src/CodeGen_Statements.cpp   – 1 site
src/Parser_Templates.cpp     – 9 sites
src/Parser_Types.cpp         – 1 site
src/ExpressionSubstitutor.cpp – 2 sites
src/Parser_Expressions.cpp   – 5 sites
src/Parser_Core.cpp          – 1 site
```

Each site calls `std::string_view::find`, which is O(n) over the string length and requires
converting a `StringHandle` → `string_view` first. The goal is to replace these with O(1) 
`StringHandle` (integer) comparisons.

---

## Root Cause of Each Check

### 1. `find("_pattern_")` — Partial Specialization Pattern Structs

When a partial specialization matches (e.g., `template<typename T, int N> struct array`),
`try_instantiate_class_template` selects the matching `TemplatePattern` from
`gTemplateRegistry.specialization_patterns_`. It then copies members from a "pattern struct"
whose name is built as:

```cpp
// Parser_Templates.cpp ~line 2485
StringBuilder pattern_name_builder;
pattern_name_builder.append(template_name).append("_pattern");
// ... append arg shape markers: _P (pointer), _A (array), _R (ref), etc.
```

The pattern struct is added to `gTypesByName` under this name.  
It is registered in `specialization_patterns_` (NOT in `templates_`), so
`gTemplateRegistry.lookupTemplate(name_handle)` returns `nullopt` for it.

The mapping from instantiated name → pattern name is tracked in
`gTemplateRegistry.instantiation_to_pattern_` (a `StringHandle → StringHandle` map),
but the inverse (pattern name → "is this a pattern?") is not stored.

### 2. `find("_unknown")` — Incomplete Instantiation Placeholders

`TemplateTypeArg::toString()` (the **old** naming scheme, `TemplateRegistry.h:374`) appends
`"unknown"` when `type_index >= gTypeInfo.size()`. This produces instantiated names like
`"is_reference_int_unknown"` when a template is instantiated while its argument type isn't
yet fully resolved.

The new `$`-hash scheme (`generateInstantiatedName`, `TemplateTypes.h:280`) always uses a
16-character hex hash separator and is unambiguous. `_unknown` names are a legacy artefact
of the old `toString()`-based naming path. Many code paths still generate or check for
them.

### 3. `find('$')` — Distinguishing Instantiations from Patterns

The `$` separator is the canonical marker that a name is a hash-based instantiation
(new scheme). It is checked in many places to decide whether a name needs further
template lookup or is already a fully instantiated type.

---

## Proposed Refactoring — 4 Phases

### Phase 1 — Add `isPatternStructName(StringHandle)` to `TemplateRegistry`

**Goal:** Replace all `find("_pattern_")` calls with a single O(1) hash lookup.

#### Implementation

Add a `std::unordered_set<StringHandle, StringHandleHash>` member to `TemplateRegistry`:

```cpp
// TemplateRegistry.h — in private section near instantiation_to_pattern_
std::unordered_set<StringHandle, StringHandleHash> pattern_struct_names_;
```

Populate it wherever a pattern struct is registered:

```cpp
// TemplateRegistry.h — modify register_instantiation_pattern
void register_instantiation_pattern(StringHandle instantiated_name, StringHandle pattern_name) {
    instantiation_to_pattern_[instantiated_name] = pattern_name;
    pattern_struct_names_.insert(pattern_name);   // NEW
}
```

Add the public query API:

```cpp
// TemplateRegistry.h — public section
bool isPatternStructName(StringHandle name) const {
    return pattern_struct_names_.count(name) > 0;
}
```

#### Call Sites to Migrate

| File | Line (approx) | Old code | New code |
|---|---|---|---|
| `CodeGen_Functions.cpp` | 463 | `struct_type_name.find("_pattern_") != npos` | `gTemplateRegistry.isPatternStructName(name_handle)` |
| `CodeGen_Functions.cpp` | 484 | `parent_for_mangling.find("_pattern_") != npos` | `gTemplateRegistry.isPatternStructName(parent_name_handle)` |
| `CodeGen_Visitors.cpp` | 315 | `type_name_view.find("_pattern_") != npos` | `gTemplateRegistry.isPatternStructName(type_name_handle)` |
| `CodeGen_Visitors.cpp` | 775 | `type_name_view2.find("_pattern_") != npos` | `gTemplateRegistry.isPatternStructName(type_name_handle)` |
| `CodeGen_Visitors.cpp` | 2878 | `parent_name.find("_pattern_") == npos` | `!gTemplateRegistry.isPatternStructName(parent_name_handle)` |
| `CodeGen_Visitors.cpp` | 3708 | `struct_name.find("_pattern_") != npos` | `gTemplateRegistry.isPatternStructName(struct_name_handle)` |

> **Note:** For call sites that currently have only a `string_view`, use
> `StringTable::getOrInternStringHandle(sv)` to get the handle (this interns the string once,
> then subsequent lookups are O(1)).

---

### Phase 2 — Add `isIncompleteInstantiation(StringHandle)` to `TypeInfo` or `TemplateRegistry`

**Goal:** Replace all `find("_unknown")` calls with a single O(1) flag or set lookup.

#### Option A — Flag on `TypeInfo` (preferred)

Add `bool is_incomplete_instantiation = false;` to `TypeInfo`:

```cpp
// AstNodeTypes.h — TypeInfo struct
bool is_incomplete_instantiation = false;  // true if created with unresolved type args
```

Set the flag whenever a placeholder TypeInfo is created with unresolved template parameters.
The key sites where `"unknown"` enters names are in `TemplateTypeArg::toString()` when
`type_index >= gTypeInfo.size()`. Before using such a placeholder in `gTypesByName`, set the flag:

```cpp
auto& placeholder_type = gTypeInfo.emplace_back(...);
placeholder_type.is_incomplete_instantiation = true;
gTypesByName.emplace(placeholder_type.name(), &placeholder_type);
```

Usage:
```cpp
// Instead of: type_name.find("_unknown") != npos
if (type_info_ptr->is_incomplete_instantiation) continue;
```

#### Option B — Set in `TemplateRegistry`

Add `std::unordered_set<StringHandle, StringHandleHash> incomplete_instantiation_names_` to
`TemplateRegistry` and register names when they are created with unresolved args:

```cpp
bool isIncompleteInstantiation(StringHandle name) const {
    return incomplete_instantiation_names_.count(name) > 0;
}
```

Option A is preferred because `TypeInfo` is already the canonical owner of per-type metadata.

#### Call Sites to Migrate

| File | Line (approx) | Old code | New code |
|---|---|---|---|
| `CodeGen_Functions.cpp` | 464 | `struct_type_name.find("_unknown") != npos` | `type_info_ptr->is_incomplete_instantiation` |
| `CodeGen_Visitors.cpp` | 321 | `type_name_view.find("_unknown") != npos` | `type_info_ptr->is_incomplete_instantiation` |
| `CodeGen_Visitors.cpp` | 780 | `type_name_view2.find("_unknown") != npos` | `type_info_ptr->is_incomplete_instantiation` |
| `CodeGen_Visitors.cpp` | 3714 | `struct_name.find("_unknown") != npos` | `type_info_ptr->is_incomplete_instantiation` |
| `CodeGen_Expressions.cpp` | 1237 | `owner_name.find("_unknown") != npos` | lookup type_info → check flag |
| `CodeGen_Statements.cpp` | 2424 | `type_name.find("_unknown") != npos` | lookup type_info → check flag |
| `Parser_Templates.cpp` | 747, 8494, 9753, 9885 | `type_name.find("_unknown") != npos` | lookup type_info → check flag |
| `Parser_Types.cpp` | 1880 | `instantiated_name.find("_unknown") != npos` | lookup type_info → check flag |

---

### Phase 3 — Replace `find('$')` with `isInstantiatedName(StringHandle)` helper

**Goal:** Replace O(n) character scans for `$` with an O(1) function.

The `$` character is the canonical separator used by `generateInstantiatedName`. The simplest
correct approach is to store a per-`TypeInfo` flag or check `instantiations_v2_` in 
`TemplateRegistry`.

#### Implementation

Add to `TemplateRegistry`:

```cpp
bool isInstantiatedName(StringHandle name) const {
    // All hash-based instantiation names are keys in instantiations_v2_
    // (or equivalently: their name was produced by generateInstantiatedName)
    auto it = instantiations_v2_.find(name);
    return it != instantiations_v2_.end();
}
```

Or, as the single cheapest approach, cache the StringHandle for `"$"` at startup and use
`StringTable` prefix comparisons — but since every check site is currently doing
`string_view.find('$')`, the cleanest solution is the registry lookup above.

#### Call Sites to Migrate

| File | Line (approx) | Old code | New code |
|---|---|---|---|
| `CodeGen_Functions.cpp` | 469 | `struct_type_name.find('$') == npos` | `!gTemplateRegistry.isInstantiatedName(name_handle)` |
| `CodeGen_Visitors.cpp` | 1528 | `name.find('$')` | `gTemplateRegistry.isInstantiatedName(name_handle)` |
| `ExpressionSubstitutor.cpp` | 485, 740 | `func_name.find('$')` | `gTemplateRegistry.isInstantiatedName(name_handle)` |
| `Parser_Core.cpp` | 153 | `type_name.find('$')` | `gTemplateRegistry.isInstantiatedName(name_handle)` |
| `Parser_Expressions.cpp` | 3081, 5072, 5921, 10853, 11422 | `…find('$')` | `gTemplateRegistry.isInstantiatedName(name_handle)` |
| `Parser_Templates.cpp` | 11274, 17282, 17458, 19499, 19789 | `…find('$')` | `gTemplateRegistry.isInstantiatedName(name_handle)` |

> Some `$` scan sites extract the **position** of the separator (to split base name from hash)
> rather than just testing for its presence. These need a different approach — either cache the
> base-name handle on the `TypeInfo`, or add `getBaseTemplateName(StringHandle) -> StringHandle`
> to `TemplateRegistry` that returns the base (pre-`$`) part. See §Future Work.

---

### Phase 4 — Long-term: Eliminate `_unknown` names entirely

The `_unknown` naming convention exists because `TemplateTypeArg::toString()` (the old
naming scheme) is still used in some code paths that haven't migrated to the
`generateInstantiatedName` / `$`-hash scheme. The ideal end-state is:

1. **Audit** all remaining callers of `TemplateTypeArg::toString()` that produce names used
   as `gTypesByName` keys or `gTypeInfo` names.
2. **Migrate** each to use `generateInstantiatedNameFromArgs` (the hash-based path) instead.
3. **Delete** the `toString()` name-based registration paths.
4. Once done, `is_incomplete_instantiation` from Phase 2 becomes unnecessary and can be
   removed, along with the `_unknown` string literal.

---

## Suggested Implementation Order

| Priority | Phase | Estimated effort | Unlocks |
|---|---|---|---|
| High | Phase 1 — `isPatternStructName` | Small: 1 set + 1 method + 6 call sites | Eliminates `_pattern_` searches in codegen |
| High | Phase 2 — `is_incomplete_instantiation` flag | Medium: 1 flag + ~10 registration sites + ~8 call sites | Eliminates `_unknown` searches in codegen |
| Medium | Phase 3 — `isInstantiatedName` | Medium: 1 method + registry integration + ~15 call sites | Eliminates `$` scans |
| Low | Phase 4 — Eliminate `_unknown` names | Large: audit all `toString()`-based name paths | Full cleanup of legacy naming scheme |

---

## Files That Will Change

```
src/TemplateRegistry.h           – Phase 1: pattern_struct_names_ + isPatternStructName
                                   Phase 3: isInstantiatedName
src/AstNodeTypes.h               – Phase 2: is_incomplete_instantiation flag on TypeInfo
src/Parser_Templates.cpp         – Phase 1: register_instantiation_pattern populate set
                                   Phase 2: set flag on placeholder TypeInfo creation
                                   Phase 3/4: migrate $ and _unknown checks
src/CodeGen_Functions.cpp        – All phases: migrate call sites
src/CodeGen_Visitors.cpp         – All phases: migrate call sites
src/CodeGen_Expressions.cpp      – Phase 2: migrate _unknown check
src/CodeGen_Statements.cpp       – Phase 2: migrate _unknown check
src/Parser_Expressions.cpp       – Phase 3: migrate $ checks
src/Parser_Core.cpp              – Phase 3: migrate $ check
src/ExpressionSubstitutor.cpp    – Phase 3: migrate $ checks
src/Parser_Types.cpp             – Phase 2: migrate _unknown check
```

---

## Non-Goals

- Changing the naming conventions themselves (`$`, `_pattern_`, `_unknown`) — the plan is to
  make the *detection* hash-based, not to rename things.
- Renaming `_unknown` types to something else as an intermediate step (this would just shift
  the problem).
- Migrating non-type-identity `$` uses (e.g., extracting the base name from an instantiated
  name for error messages) — these are string processing tasks where substring search is
  appropriate and cannot be replaced with a simple set lookup.

---

## Testing Strategy

After each phase:
1. Run `tests/run_all_tests.sh` to confirm no regressions.
2. Run `tests/std/` header tests (array, latch, etc.) to confirm standard-header
   compilation still works.
3. Add a targeted regression test in `tests/` for each structural change (e.g., a test that
   instantiates a partial specialization and calls its member function, verifying codegen
   doesn't trip over a pattern struct).

---

## Known Issues / Caveats

- **`$` position extraction:** ~10 sites use `find('$')` to extract the base-name prefix
  (e.g., `"array"` from `"array$hash"`). These are string-manipulation operations, not
  mere presence checks, and are out of scope for Phase 3. A future `TypeInfo::base_template_`
  field (which already exists as `QualifiedIdentifier`) could serve as the hash-based
  replacement for those sites too.
- **`instantiation_to_pattern_` completeness:** Not all partial specialization pattern structs
  go through `register_instantiation_pattern`. Phase 1 must audit all paths where a pattern
  struct name is added to `gTypesByName` and ensure the set is populated at each site.
- **Thread safety:** The new sets follow the same single-threaded model as the existing
  `TemplateRegistry` maps. No additional synchronization is needed.
