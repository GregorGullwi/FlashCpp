# Type system consolidation: audit and migration roadmap

**Date**: 2026-03-25  
**Status**: Phase 1 (Option A) complete. Phases 2-5 (Option C) pending.  
**Related docs**: `docs/2026-03-12_ENUM_IR_LOWERING_PLAN.md`

---

## Implementation status

| Phase | Description | Status |
| --- | --- | --- |
| 1 | Add `is_primitive_type(Type)` and `needs_type_index(Type)` helpers | ✅ Done (this PR) |
| 1 | Add `static_assert` compile-time coverage for the new helpers | ✅ Done (this PR) |
| 1 | Replace 76 raw `Struct/UserDefined` and `Struct/Enum/UserDefined` disjunctions across 25 files | ✅ Done (this PR) |
| 1 | Make `binaryOperatorUsesTypeIndexIdentity` and `carriesSemanticTypeIndex` delegate to `needs_type_index` | ✅ Done (this PR) |
| 1 | Add `is_builtin_type(Type)` helper; replace 2 enum-order range checks | ✅ Done (this PR) |
| 1 | Add canonical `constexpr isArithmeticType`/`isFundamentalType`; deduplicate 3 local copies | ✅ Done (this PR) |
| 2 | Add `TypeInfo` query helpers (`isStructLike`, `isPrimitive`, `needsTypeIndex`, `isTemplatePlaceholder`) | ⬜ TODO |
| 3 | Convert mixed `Type` + `TypeIndex` call sites to prefer `TypeIndex` as the source of truth | ⬜ TODO |
| 4 | Consolidate `is_integral_type` / `isIntegralType` to one definition | ⬜ TODO |
| 5 | Audit remaining `Type`-only consumers and decide whether `Type` stays as a cached category | ⬜ TODO |

---

## 1. Current state analysis

### Dual-identifier model

The semantic type model is split across two identifiers:

- `Type` (an `int_fast16_t`-backed enum) answers fast category questions (`Struct`, `Enum`, `Int`, `Template`, ...).
- `TypeIndex` (a `size_t` wrapper) identifies a concrete entry in `gTypeInfo`.

That split works, but the audit of commit `8a0f990` (the pre-consolidation baseline for this
branch) found multiple ad-hoc ways to ask the same classification question:

| Pattern family | Pre-cleanup count | Post-cleanup count | Notes |
| --- | ---: | ---: | --- |
| `x == Type::Struct \|\| x == Type::UserDefined` (or reversed) | 59 | 0 | Replaced by `is_struct_type(x)` |
| `x != Type::Struct && x != Type::UserDefined` (or reversed) | 18 | 0 | Replaced by `!is_struct_type(x)` |
| `x == Type::Struct \|\| x == Type::Enum \|\| x == Type::UserDefined` | 8 | 0 | Replaced by `needs_type_index(x)` |
| `x != Type::Struct && x != Type::Enum && x != Type::UserDefined` | 1 | 0 | Replaced by `!needs_type_index(x)` |
| Isolated `!= Type::Struct` or `== Type::Struct` | n/a | 51 | Contextually specific; not mechanical duplications |
| Isolated `== Type::Enum` / `!= Type::Enum` | n/a | 47 | Intentional semantic checks per `ENUM_IR_LOWERING_PLAN.md` |
| Isolated `!= Type::UserDefined` / `== Type::UserDefined` | n/a | 14 | Mix of alias-resolution and substitution context |
| `Type::Template` comparisons | 6 | 6 | Unresolved placeholder semantics; not yet consolidated |
| `Type::Template \|\| Type::UserDefined` | 3 | 3 | Template param substitution context |
| `arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer` | 2 | 2 | Range-based primitive/pointer check |
| `Bool <= x <= LongDouble` (arithmetic range check) | 4 | 4 | Duplicated helper in 3 separate translation units |

### Inconsistencies that remain

1. **`Struct || UserDefined` (struct-like object) vs `Struct || Enum || UserDefined` (type with identity)**  
   Both are now behind named helpers (`is_struct_type` and `needs_type_index`), so the distinction is explicit. However there are still many single-type `!= Type::Struct` checks in codegen/IR that intentionally exclude UserDefined (they want a Struct-typed ExprResult from IR, not a UserDefined alias). These are _not_ bugs — they reflect that IR cares about resolved concrete struct types, not unresolved aliases.

2. **`Type::Template` is sometimes treated like `UserDefined`, but not consistently**  
   - `ExpressionSubstitutor` groups `Template` with `UserDefined` (`ExpressionSubstitutor.cpp:661`, `:981-982`).
   - `TemplateInstantiationHelper` checks `Type::Template` directly (`:305`, `:334`).
   - The semantic layer ignores `Template` in the classification helpers added in Phase 1.
   - This is intentional for now — `Type::Template` represents an **unresolved placeholder**, unlike the concrete `Struct`/`Enum`/`UserDefined` types.

3. **`isArithmeticType` / `isFundamentalType` are duplicated three times**  
   The same range-based implementation (`Bool(1) through LongDouble(14)`) lives in:
   - `TypeTraitEvaluator.cpp:5-13`
   - `IrGenerator_MemberAccess.cpp:2119-2129`
   - `ConstExprEvaluator_Members.cpp:5423-5430`
   
   These should be moved to `AstNodeTypes_TypeSystem.h` as `constexpr` functions and the local duplicates removed.

4. **`arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer` range check**  
   This appears in `Parser_Core.cpp:65` and `Parser_Templates_Inst_Deduction.cpp:32`. The intent is "this is a builtin type that `get_type_size_bits()` can handle". The existing `is_primitive_type()` helper does **not** cover this range because it excludes `FunctionPointer`, `MemberFunctionPointer`, `MemberObjectPointer`, `Auto`, and `DeclTypeAuto`, which sit between `LongDouble` and `UserDefined` in enum order. A new helper `is_builtin_type(Type)` (true for `Void` through `MemberObjectPointer`) would clean these two sites up without breaking the arithmetic-type semantics.

### Existing helpers: coverage after Phase 1

| Helper | Location | Covers | Limitations |
| --- | --- | --- | --- |
| `is_struct_type(Type)` | `src/AstNodeTypes.cpp:259-260` | `Struct`, `UserDefined` | Excludes `Enum` and `Template`; intentional |
| `is_primitive_type(Type)` | `src/AstNodeTypes_TypeSystem.h:336-362` | `Void` through `LongDouble`, `Nullptr` | Does not cover pointer-likes or `Auto`/`DeclTypeAuto` |
| `needs_type_index(Type)` | `src/AstNodeTypes_TypeSystem.h:364-366` | `Struct`, `Enum`, `UserDefined` | Does not cover `Template` (intentional) |
| `carriesSemanticTypeIndex(Type)` | `src/IrType.h:86` | delegates to `needs_type_index` | IR-layer alias; use `needs_type_index` in semantic code |
| `binaryOperatorUsesTypeIndexIdentity(Type)` | `src/OverloadResolution.h:1258` | delegates to `needs_type_index` | Overload-resolution alias; semantically identical to `needs_type_index` |
| `isIntegralType(Type)` | `src/AstNodeTypes_TypeSystem.h:461` | integral primitives incl. char types and `Bool` | — |
| `isFloatingPointType(Type)` | `src/AstNodeTypes_TypeSystem.h:349` | `Float`, `Double`, `LongDouble` | — |
| `isUnsignedIntegralType(Type)` | `src/AstNodeTypes_TypeSystem.h:353` | unsigned integral types | target-dependent for `WChar` |
| `is_integral_type(Type)` | `src/OverloadResolution.h:45-47` | `Bool` + integer primitives | duplicate concept to `isIntegralType`; lives in wrong header |
| `isArithmeticType(Type)` | duplicated in 3 TUs | `Bool` through `LongDouble` | Moved to `AstNodeTypes_TypeSystem.h:449` (this PR); local copies delegate |
| `isFundamentalType(Type)` | duplicated in 3 TUs | `Void`, `Nullptr`, arithmetic | Moved to `AstNodeTypes_TypeSystem.h:449` (this PR); local copies delegate |
| `isPlaceholderAutoType(Type)` | `src/AstNodeTypes_TypeSystem.h:322` | `Auto`, `DeclTypeAuto` | — |

### Key locations where `Type` and `TypeIndex` are used together

These are the most important sites for a future Option C migration:

- **Alias resolution**: `src/AstNodeTypes_DeclNodes.h:837-845`, `src/AstNodeTypes_DeclNodes.h:1174-1185`
- **Template hashing/equality**: `src/TemplateRegistry_Types.h:290-320`, `src/TemplateRegistry_Types.h:455-493`
- **Binary operator operand identity**: `src/OverloadResolution.h:1262-1303`, `src/IrGenerator_Expr_Operators.cpp:1392-1401`
- **Substitution and template lookup**: `src/ExpressionSubstitutor.cpp:981-984`, `src/Parser_Templates_Inst_Substitution.cpp:612-614`

### Places that rely on `Type` alone and would need migration under Option B/C

- **Name mangling**: `src/NameMangling.h:174-186` (switch on `Type::Struct`/`Enum`/`UserDefined`)
- **Type-trait classification**: `src/TypeTraitEvaluator.cpp:5-13` (range-based arithmetic check)
- **IR-lowering category mapping**: `src/IrType.cpp` (full switch over `Type`)
- **Parser primitive-range checks**: `src/Parser_Core.cpp:65`, `src/Parser_Templates_Inst_Deduction.cpp:32`
- **Semantic-analysis branching**: `src/SemanticAnalysis.cpp:2968-2972`, `src/TemplateInstantiationHelper.h:305-334`

---

## 2. Option A: Keep `Type`, add helper functions ✅ IMPLEMENTED

### What was done

- Added `constexpr bool is_primitive_type(Type)` and `constexpr bool needs_type_index(Type)` in `src/AstNodeTypes_TypeSystem.h`.
- Added a comment block explaining the classification model.
- Added `static_assert` compile-time coverage for all representative cases.
- Replaced 76 raw classification disjunctions across 25 source files:
  - All `x == Type::Struct || x == Type::UserDefined` (or negated) → `is_struct_type(x)` / `!is_struct_type(x)`
  - All `x == Type::Struct || x == Type::Enum || x == Type::UserDefined` (or negated) → `needs_type_index(x)` / `!needs_type_index(x)`
- Made `binaryOperatorUsesTypeIndexIdentity` and `carriesSemanticTypeIndex` delegate to `needs_type_index` instead of repeating the disjunction.

### Remaining Option A TODOs

These are the straightforward next cleanups that do **not** require architectural changes:

**TODO 1 — Add `is_builtin_type(Type)`**  
Replace the two `arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer` range checks in `Parser_Core.cpp:65` and `Parser_Templates_Inst_Deduction.cpp:32`. The intent is "this type has a valid `get_type_size_bits()` answer". A constexpr switch-based helper avoids the enum-ordering dependency.

```cpp
// Proposed addition to AstNodeTypes_TypeSystem.h:
constexpr bool is_builtin_type(Type type) {
    switch (type) {
    case Type::Void: case Type::Bool: case Type::Char: case Type::UnsignedChar:
    case Type::WChar: case Type::Char8: case Type::Char16: case Type::Char32:
    case Type::Short: case Type::UnsignedShort: case Type::Int: case Type::UnsignedInt:
    case Type::Long: case Type::UnsignedLong: case Type::LongLong: case Type::UnsignedLongLong:
    case Type::Float: case Type::Double: case Type::LongDouble:
    case Type::FunctionPointer: case Type::MemberFunctionPointer: case Type::MemberObjectPointer:
        return true;
    default: return false;
    }
}
```

**TODO 2 — Deduplicate `isArithmeticType` / `isFundamentalType`**  
The same range-based implementations live in three separate translation units. Move them to `AstNodeTypes_TypeSystem.h` as `constexpr` functions (replacing the range check with an explicit switch like `is_primitive_type`), then delete the local copies in `TypeTraitEvaluator.cpp`, `IrGenerator_MemberAccess.cpp`, and `ConstExprEvaluator_Members.cpp`.

```cpp
// Proposed addition to AstNodeTypes_TypeSystem.h:
constexpr bool isArithmeticType(Type type) {
    switch (type) {
    case Type::Bool: case Type::Char: /* all numeric types */ case Type::LongDouble:
        return true;
    default: return false;
    }
}
constexpr bool isFundamentalType(Type type) {
    return type == Type::Void || type == Type::Nullptr || isArithmeticType(type);
}
```

**TODO 3 — Consolidate `is_integral_type` location**  
`is_integral_type(Type)` in `src/OverloadResolution.h:45-47` duplicates the intent of `isIntegralType(Type)` already in `AstNodeTypes_TypeSystem.h:383`. Move `is_integral_type` to the type system header (or replace it with `isIntegralType` at call sites) and keep one canonical definition.

### Pros
- Smallest, safest change
- Backward compatible with the current AST, parser, semantic, and mangling code
- Easy to review and test incrementally

### Cons
- Keeps two parallel identification systems (`Type` and `TypeIndex`)
- Enum-ordering dependency in range checks (partially addressed by TODO 2)
- Does not answer the `Type::Template` question

---

## 3. Option B: Extend `TypeIndex` to replace `Type` entirely

### Sketch

A full replacement would make `TypeIndex` the only type identifier carried through the semantic layer.

**Packed-tagged-value design**:
- `uint32_t payload` — low bits encode category flags (`primitive`, `enum`, `template placeholder`, ...), remaining bits encode a primitive id or `gTypeInfo` index.

**Reserved-index design**:
- Reserve a fixed range of `TypeIndex` values (e.g., 0-31) for primitive/builtin kinds.
- All user types continue to live in `gTypeInfo`.
- `type_index.isPrimitive()`, `type_index.primitiveKind()`, etc.

### Why this is not the right next step

1. Primitive `TypeIndex` values are assigned **at runtime** via `gTypeInfo.emplace_back(...)` in `initialize_native_types()` (`AstNodeTypes.cpp:133-218`). Making them constexpr would require moving builtin metadata out of the runtime table — significant bootstrap surgery.
2. Every `switch` over `Type` (name mangling, IR lowering, type traits, parser) would need simultaneous rewriting.
3. The ENUM_IR_LOWERING_PLAN (`docs/2026-03-12_ENUM_IR_LOWERING_PLAN.md`) already intentionally retains `Type::Enum` in the semantic layer.

### Performance note

Today, `Type` comparisons are trivial integer compares. An extended `TypeIndex` can be equally fast **only if** classification metadata is inline/tagged. A naïve "look up `gTypeInfo`" approach adds cache-miss risk to hot paths like overload resolution and type-trait evaluation.

---

## 4. Option C: Hybrid — `TypeIndex` as primary identity, `Type` as cached category ← **recommended long-term direction**

### Core idea

`TypeIndex` becomes the authoritative handle for type **identity** wherever it is valid. `Type` is retained as a fast cached **category** inside `TypeInfo`, not a free-floating tag in AST nodes.

This matches the codebase as it already works:
- `gTypeInfo` already stores both category and identity together (`TypeInfo::type_` + `TypeInfo::type_index_`).
- Many subsystems already read both (`AstNodeTypes_DeclNodes.h:837-845`, `TemplateRegistry_Types.h:290-320`, `OverloadResolution.h:1262-1303`).
- Hot classification queries stay cheap — they read `TypeInfo::type_` instead of a table lookup.

### What changes

#### Step A — Add `TypeInfo` query helpers (prerequisite for TypeIndex-centric code)

`TypeInfo` already has `isStruct()` (returns `type_ == Type::Struct`) and `isEnum()` (returns `type_ == Type::Enum`), but it is missing:

```cpp
// Proposed additions to TypeInfo in AstNodeTypes_DeclNodes.h:
bool isStructLike()  const { return is_struct_type(type_); }           // Struct OR UserDefined
bool isPrimitive()   const { return is_primitive_type(type_); }
bool needsTypeIndex() const { return ::needs_type_index(type_); }
bool isTemplatePlaceholder() const { return type_ == Type::Template; }
```

Callers can then write `gTypeInfo[idx].isStructLike()` instead of `gTypeInfo[idx].type_ == Type::Struct || gTypeInfo[idx].type_ == Type::UserDefined`.

#### Step B — Replace mixed `type_` + manual comparisons at key hot spots

Current pattern in `AstNodeTypes_DeclNodes.h:837-845`:
```cpp
if (type == Type::UserDefined && type_index.is_valid() && ...) {
    if (!needs_type_index(type_info.type_)) { ... }
}
```
After Step A, this becomes readable as an `isStructLike()` / `isPrimitive()` chain.

The most impactful migration sites (in order of call frequency):
1. `TemplateRegistry_Types.h` — type hashing/equality (already uses `needs_type_index`)
2. `OverloadResolution.h` — operand type identity checks
3. `ExpressionSubstitutor.cpp` — template param substitution
4. `SemanticAnalysis.cpp` — implicit conversion planning

#### Step C — Replace enum-order-dependent arithmetic/fundamental checks

After TODO 2 above moves `isArithmeticType`/`isFundamentalType` to `constexpr` switch-based functions in `AstNodeTypes_TypeSystem.h`, the three local copies can be deleted and the TypeInfo helpers can call the canonical functions.

#### Step D — Decide `Type::Template` story

`Type::Template` represents an **unresolved template type placeholder** during template parsing/substitution, not a concrete type. It is not the same as `UserDefined` (a resolved typedef alias). The two choices are:

1. **Keep `Type::Template` separate** and add explicit `isTemplatePlaceholder()` query everywhere it appears (6 current sites). This is clean and explicit.
2. **Fold `Type::Template` into `UserDefined`** and use the `TypeInfo::is_incomplete_instantiation_` flag or a new field to distinguish placeholders. This reduces the enum cardinality but requires touching template substitution code.

Option D.1 is recommended: it is safer and makes the distinction visible at call sites.

#### Step E — Gradually migrate or narrow `Type` enum

Once `TypeIndex`-centric helpers are available and the arithmetic helpers are deduplicated, the remaining `Type` comparisons in codegen and mangling are intentional (they answer "what IR representation does this type need?" rather than "what is this type's identity?"). At that point the `Type` enum's role is clearly a category tag rather than an identity system, which is the Option C end state.

The `IrType` enum already models this correctly for IR/codegen (`docs/2026-03-12_ENUM_IR_LOWERING_PLAN.md`). Option C simply extends that principle to the semantic layer.

### Pros
- Single authoritative identity token (`TypeIndex`)
- Cheap classification stays cheap (cached `type_` field in `TypeInfo`)
- Incremental migration — each step is independently testable
- Compatible with the existing ENUM_IR_LOWERING_PLAN architecture

### Cons
- Retains two pieces of data (`TypeIndex` + cached `type_`), but with clarified roles
- Requires disciplined API boundary (helpers on `TypeInfo`, not raw `type_` access)

---

## 5. Recommendation and migration plan

### Recommended path: **Option C, staged through remaining Option A TODOs**

Option A is done. The recommended next steps follow the Option C progression:

#### Milestone 1 — Complete Option A remaining TODOs (low risk, high value)

- [x] **TODO 1**: Add `is_builtin_type(Type)` helper and replace the two range checks in `Parser_Core.cpp:65` and `Parser_Templates_Inst_Deduction.cpp:32`.
- [x] **TODO 2**: Move `isArithmeticType` and `isFundamentalType` to `AstNodeTypes_TypeSystem.h` as `constexpr` switch-based helpers. Delete the three duplicated local copies.
- [ ] **TODO 3**: Consolidate `is_integral_type` / `isIntegralType` to one definition in `AstNodeTypes_TypeSystem.h`.

#### Milestone 2 — Add `TypeInfo` query helpers (Option C Step A)

- [ ] Add `isStructLike()`, `isPrimitive()`, `needsTypeIndex()`, `isTemplatePlaceholder()` to `TypeInfo` in `AstNodeTypes_DeclNodes.h`.
- [ ] Replace the most redundant `gTypeInfo[idx].type_ == Type::X` patterns with the new helpers. Priority: `TemplateRegistry_Types.h` and `OverloadResolution.h`.

#### Milestone 3 — Migrate mixed `Type` + `TypeIndex` hot spots (Option C Step B/C)

- [ ] Convert the mixed-identity sites in `ExpressionSubstitutor.cpp` and `SemanticAnalysis.cpp` to use `TypeIndex` as primary, reading category from `TypeInfo` methods.
- [ ] Replace the alias-resolution pattern in `AstNodeTypes_DeclNodes.h:837-845` with explicit `TypeInfo` helper calls.

#### Milestone 4 — Settle `Type::Template` (Option C Step D)

- [ ] Add `TypeInfo::isTemplatePlaceholder()` and replace the 6 raw `type_ == Type::Template` comparisons with the helper call.
- [ ] Document in code why `Type::Template` is not part of `needs_type_index()`.

#### Milestone 5 — Audit and narrow remaining raw `Type` consumers

- [ ] Audit `NameMangling.h`, `IrType.cpp`, and codegen `!= Type::Struct` single-type checks.
- [ ] Decide whether `Type` should be narrowed to a smaller `TypeCategory` enum covering only the codegen-relevant categories, or left as-is with clear documentation that it is a cache, not an identity system.

### `Type::Template` decision (immediate)

Do **not** fold `Type::Template` into `needs_type_index()` yet. Template placeholders are semantically different from concrete `Struct` / `Enum` / `UserDefined` types and require different handling during substitution. Adding `isTemplatePlaceholder()` to `TypeInfo` (Milestone 4) is the right way to make this boundary explicit.
