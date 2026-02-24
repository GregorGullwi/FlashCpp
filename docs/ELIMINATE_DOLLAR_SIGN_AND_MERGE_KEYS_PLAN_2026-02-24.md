# Plan: Eliminate `$` from Instantiated Names & Merge Template Instantiation Keys

**Date:** 2026-02-24  
**Context:** Follow-up to the hash-based name refactoring (Phases 1–4) that replaced
`find("_pattern_")`, `find("_unknown")`, and boolean `find('$')` with O(1) lookups.

---

## Problem Statement

### 1. `$` separator in instantiated names

Template instantiation names currently embed a `$` character as a separator between
the base template name and a 16-hex-digit hash of the template arguments:

```
vector$a1b2c3d4e5f6g7h8
```

While `$` cannot appear in standard C++ identifiers (making it unambiguous), it creates
several problems:

- **12 call sites** still use `find('$')` to extract the position and split the name
  into base-template and hash portions. These are O(n) substring scans.
- The `$` separator couples the naming scheme to string parsing — any code that needs
  the base template name must parse the string rather than using structured metadata.
- `TemplateTypeArg::toString()` and `typeToString()` still emit `"$unresolved"` for
  unresolved types, requiring `updateIncompleteInstantiationFlag()` to scan names for
  this sentinel.

### 2. Two template instantiation key structs

Two key types coexist for template instantiation lookup:

| Key | Defined in | Backing map | Usage count |
|-----|-----------|-------------|-------------|
| `TemplateInstantiationKey` | `TemplateRegistry.h:540` | `instantiations_` | ~6 call sites (Parser_Templates.cpp, CodeGen_Functions.cpp) |
| `TemplateInstantiationKeyV2` | `TemplateTypes.h:141` | `instantiations_v2_` | ~3 call sites (TemplateRegistry.h, Parser_Expressions.cpp) |

**`TemplateInstantiationKey` (V1)** uses `InlineVector<Type>` + `InlineVector<TypeIndex>` +
`InlineVector<int64_t>` + `InlineVector<StringHandle>` — four separate vectors that
duplicate information already available in V2's `TypeIndexArg`.

**`TemplateInstantiationKeyV2`** uses `InlineVector<TypeIndexArg, 4>` which includes
`TypeIndex`, `Type`, cv-qualifiers, reference qualifier, pointer depth, and array info
in a single struct per argument — strictly more capable and precise.

Having two key types and two maps means:
- Duplicate storage for every instantiation
- Risk of inconsistency between the two maps
- Confusing API (`hasInstantiation` vs `getInstantiationV2`)

---

## Proposed Refactoring — 3 Phases

### Phase 1 — Store base template name on TypeInfo (eliminate position-extraction `find('$')`)

**Goal:** Remove all 12 remaining `find('$')` call sites that extract the base template
name from the instantiated name string.

#### Current state

`TypeInfo` already has:
```cpp
QualifiedIdentifier base_template_;  // set via setTemplateInstantiationInfo()
bool isTemplateInstantiation() const { return base_template_.valid(); }
StringHandle baseTemplateName() const { return base_template_.identifier_handle; }
```

This metadata is populated for class template instantiations in `try_instantiate_class_template`.

#### What's missing

Not all instantiated names have a corresponding `TypeInfo` with `base_template_` set:
- **Function template instantiations** — function names are in the symbol table, not gTypeInfo
- **Variable template instantiations** — similar to functions

#### Implementation

1. **Audit all 12 `find('$')` sites** to determine what each extracts:

| File | Line | What it extracts | Can use TypeInfo? |
|------|------|-----------------|-------------------|
| `CodeGen_Functions.cpp` | 545 | base name from func name | No — need symbol-level metadata |
| `CodeGen_Visitors.cpp` | 1525 | base name from type name | Yes |
| `ExpressionSubstitutor.cpp` | 519 | base name from func name | No |
| `ExpressionSubstitutor.cpp` | 774 | base name from namespace | No |
| `Parser_Core.cpp` | 153 | base name from type name | Yes |
| `Parser_Expressions.cpp` | 10872 | base name from class name | Yes |
| `Parser_Expressions.cpp` | 11441 | base name from type name | Yes |
| `Parser_Templates.cpp` | 11283 | base name from struct name | Yes |
| `Parser_Templates.cpp` | 17295 | base name from struct name | Yes |
| `Parser_Templates.cpp` | 17471 | base name from struct name | Yes |
| `Parser_Templates.cpp` | 19512 | base name from func name | No |
| `Parser_Templates.cpp` | 19802 | base name from struct name | Yes |

2. **For TypeInfo-backed sites (8 of 12):** Replace `find('$')` + `substr` with
   `TypeInfo::baseTemplateName()` lookup via `gTypesByName`.

3. **For function/symbol-backed sites (4 of 12):** Add `base_template_name` metadata
   to the symbol table entry or function declaration node. This can be a `StringHandle`
   field set when the function template is instantiated.

#### Stretch goal

Once all 12 sites use metadata instead of string parsing, the `$` separator in
`generateInstantiatedName()` becomes an internal implementation detail that no code
outside of name generation needs to know about. At that point, the separator could be
changed to any other scheme (or removed entirely, using only the hash) without affecting
any other code.

---

### Phase 2 — Eliminate `$unresolved` from names

**Goal:** Remove `"$unresolved"` from `TemplateTypeArg::toString()` and `typeToString()`,
and remove `updateIncompleteInstantiationFlag()` name scanning.

#### Implementation

1. **Set `is_incomplete_instantiation_` directly** at the point where placeholder TypeInfo
   entries are created with unresolved template arguments, rather than scanning the name.
   The flag should be set explicitly in `add_struct_type()` or `setTemplateInstantiationInfo()`
   when any template argument is dependent/unresolved.

2. **Change `toString()` to return a deterministic placeholder** that doesn't need to appear
   in the type name. Since `toString()` output flows into `generateInstantiatedNameFromArgs()`
   which hashes it, the actual string content doesn't matter for uniqueness — only
   consistency. Use a fixed sentinel like `"\x01"` (non-printable, guaranteed not in any
   identifier) or simply `"?"`.

3. **Remove `updateIncompleteInstantiationFlag()`** — the flag is set directly, not derived
   from the name.

4. **Remove the last `find("$unresolved")` in `AstNodeTypes.h:1280`** — no longer needed.

---

### Phase 3 — Merge `TemplateInstantiationKey` and `TemplateInstantiationKeyV2`

**Goal:** Unify into a single key type and a single instantiation map.

#### Analysis

| Feature | V1 (`TemplateInstantiationKey`) | V2 (`TemplateInstantiationKeyV2`) |
|---------|--------------------------------|-----------------------------------|
| Template name | `StringHandle` | `StringHandle` |
| Type args | `InlineVector<Type>` + `InlineVector<TypeIndex>` | `InlineVector<TypeIndexArg, 4>` (includes Type, TypeIndex, cv, ref, ptr, array) |
| Value args | `InlineVector<int64_t>` | `InlineVector<int64_t, 4>` |
| Template args | `InlineVector<StringHandle>` | `InlineVector<StringHandle, 2>` |
| CV/ref/ptr | Not tracked | Tracked in `TypeIndexArg` |

V2 is strictly more capable. V1 cannot distinguish `const int` from `int` as template
arguments because it doesn't track cv-qualifiers.

#### Implementation

1. **Migrate all V1 call sites to V2:**
   - `Parser_Templates.cpp`: 4 sites build `TemplateInstantiationKey` — convert to
     `makeInstantiationKeyV2()` using the existing `TemplateTypeArg` vectors
   - `CodeGen_Functions.cpp`: 1 site builds `TemplateInstantiationKey` — convert similarly
   - `TemplateRegistry.h`: `hasInstantiation()`, `getInstantiation()`, `registerInstantiation()`
     — redirect to V2 map or replace

2. **Remove V1 types and map:**
   - Delete `struct TemplateInstantiationKey` and `TemplateInstantiationKeyHash`
   - Delete `instantiations_` map
   - Rename `instantiations_v2_` → `instantiations_`
   - Rename `getInstantiationV2` → `getInstantiation`, etc.

3. **Rename V2 to remove version suffix:**
   - `TemplateInstantiationKeyV2` → `TemplateInstantiationKey`
   - `TemplateInstantiationKeyV2Hash` → `TemplateInstantiationKeyHash`
   - `makeInstantiationKeyV2` → `makeInstantiationKey`

---

## Suggested Implementation Order

| Priority | Phase | Estimated effort | Dependencies |
|----------|-------|-----------------|-------------|
| High | Phase 3 — Merge keys | Medium (6 call sites + rename) | None |
| Medium | Phase 2 — Eliminate `$unresolved` | Small (flag + toString change) | None |
| Medium | Phase 1 — Eliminate `find('$')` | Large (12 sites, some need new metadata) | Phase 2 (for clean separation) |

Phase 3 (merge keys) is independent and should be done first since it reduces confusion
and makes the codebase easier to reason about for subsequent changes.

---

## Files That Will Change

```
src/TemplateRegistry.h          — Phase 3: remove V1 key/map, rename V2
                                  Phase 1: no changes (metadata already exists)
src/TemplateTypes.h             — Phase 3: rename V2 types
                                  Phase 2: remove $unresolved from generateInstantiatedName
src/AstNodeTypes.h              — Phase 2: remove updateIncompleteInstantiationFlag()
src/Parser_Templates.cpp        — Phase 3: migrate V1 key construction sites
                                  Phase 1: migrate find('$') sites
src/Parser_Expressions.cpp      — Phase 1: migrate find('$') sites
src/Parser_Core.cpp             — Phase 1: migrate find('$') site
src/CodeGen_Functions.cpp       — Phase 3: migrate V1 key construction
                                  Phase 1: migrate find('$') site
src/CodeGen_Visitors.cpp        — Phase 1: migrate find('$') site
src/ExpressionSubstitutor.cpp   — Phase 1: migrate find('$') sites
src/NameMangling.h              — Phase 1: migrate $ usage in name mangling
```

---

## Testing Strategy

After each phase:
1. Run `tests/run_all_tests.sh` (1136 tests) to confirm no regressions
2. Run `tests/std/` header tests for standard library template coverage
3. Verify template-heavy tests specifically:
   - `test_unknown_template_name_ret0.cpp` (names with "unknown")
   - `test_template_*.cpp` (various template patterns)
   - `test_std_*.cpp` (standard library usage)
