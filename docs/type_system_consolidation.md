# Type system consolidation: audit and migration roadmap

**Date**: 2026-03-25  
**Status**: Phase 1 (Option A) complete. Milestone 1 TODO 3 done. Milestone 2 TypeInfo helpers and sentinel done. Milestone 2.5 `is_type_alias_` flag done. Milestone 4 `isTemplatePlaceholder()` done. Milestone 6 (Option D Step 0) `gTypeInfo` accessor API done. Milestone 7 Steps 1+2 done: `TypeCategory` enum defined, embedded in `TypeIndex`, and TypeCategory-based classification helpers added.  
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
| 2 | Add `TypeInfo` query helpers (`isStructLike`, `isPrimitive`, `needsTypeIndex`, `isTemplatePlaceholder`) | ✅ Done |
| 2 | Resolve `Type::UserDefined` ambiguity: split enum or add `is_type_alias` flag (§7.1) | ✅ Done (`is_type_alias_` flag + `isTypeAlias()`) |
| 2 | Add `static_assert` enum-count sentinel for `Type` (§7.3) | ✅ Done (`Type::Count_` + static_assert) |
| 3 | Convert mixed `Type` + `TypeIndex` call sites to prefer `TypeIndex` as the source of truth | ⬜ TODO |
| 3 | Upgrade or document `resolve_type_alias` chain-following behavior (§7.1 option b) | ⬜ TODO |
| 3 | Document `buildConversionPlan` as legitimate `Type`-primary consumer (§7.2) | ⬜ TODO |
| 4 | Consolidate `is_integral_type` / `isIntegralType` to one definition | ✅ Done (removed `is_integral_type`; use `isIntegralType`) |
| 5 | Audit remaining `Type`-only consumers and decide whether `Type` stays as a cached category | ⬜ TODO |
| 6 | Create `gTypeInfo` accessor API — Option D Step 0 (§5, Milestone 6) | ✅ Done (`getTypeInfo`, `getTypeInfoMut`, `findTypeByName`, `findNativeType`, `getTypeInfoCount`, `forEachTypeInfo`; `extern` declarations removed) |
| 7 | Add `TypeCategory`, embed in `TypeIndex`, migrate all `Type` usages — Option D Steps 1-3 (§5, Milestone 7) | 🔄 Steps 1+2 done; Step 3 (site migration + delete `Type`) TODO |
| — | Resolve `Type::UserDefined` semantic ambiguity (§7.1) — prerequisite for Milestone 3 | ⬜ TODO |
| — | Migrate `buildConversionPlan` with dedicated test coverage (§7.2) | ⬜ TODO |

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
| `is_integral_type(Type)` | removed from `src/OverloadResolution.h` | `Bool` + integer primitives | **merged into `isIntegralType`** — all call sites updated |
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

## 5. Option D: Embed `TypeCategory` inside `TypeIndex`, hide `gTypeInfo` behind accessors

### Proposal overview

Option D was proposed as a concrete next step after Option C. It makes two reinforcing changes:

1. **Add a new `TypeCategory` enum** (mirroring the values of `Type`, but a distinct C++ type) and embed it as a field inside `TypeIndex` alongside the index. Every `TypeIndex` becomes self-describing — no `gTypeInfo` lookup needed for category queries, and no separate `Type` field to keep in sync.

2. **Hide `gTypeInfo` behind accessor functions** (`getTypeInfo()`, `addStructType()`, `isValid()`, etc.) so that no code outside `AstNodeTypes.cpp` touches the global table directly.

The old `Type` enum stays in place initially. Because `TypeCategory` is a **different C++ type** from `Type`, the compiler reports every site that still uses the old enum — giving a completely mechanical migration path.

### Proposed `TypeCategory` enum

```cpp
// Distinct from Type so the compiler enforces migration site-by-site.
// Explicit values match Type 0-23 exactly, then TypeAlias=24 is inserted,
// shifting Auto and later values by 1.
enum class TypeCategory : uint8_t {
    Invalid             = 0,
    Void                = 1,
    Bool                = 2,
    Char                = 3,
    UnsignedChar        = 4,
    WChar               = 5,
    Char8               = 6,
    Char16              = 7,
    Char32              = 8,
    Short               = 9,
    UnsignedShort       = 10,
    Int                 = 11,
    UnsignedInt         = 12,
    Long                = 13,
    UnsignedLong        = 14,
    LongLong            = 15,
    UnsignedLongLong    = 16,
    Float               = 17,
    Double              = 18,
    LongDouble          = 19,
    FunctionPointer     = 20,
    MemberFunctionPointer = 21,
    MemberObjectPointer = 22,
    UserDefined         = 23,
    TypeAlias           = 24,  // NEW — split from old Type::UserDefined (see §7.1)
    Auto                = 25,
    DeclTypeAuto        = 26,
    Function            = 27,
    Struct              = 28,
    Enum                = 29,
    Nullptr             = 30,
    Template            = 31,
};

// Correspondence checks: values 0-23 match the old Type enum exactly.
static_assert(static_cast<int>(TypeCategory::Int)  == static_cast<int>(Type::Int));
static_assert(static_cast<int>(TypeCategory::Struct) == static_cast<int>(Type::Struct) + 1);
// The +1 for Struct and later reflects TypeAlias insertion; document prominently.
```

### Proposed `TypeIndex` struct

```cpp
struct TypeIndex {
    uint32_t     index_    = 0;
    TypeCategory category_ = TypeCategory::Invalid;

    constexpr TypeIndex() noexcept = default;
    constexpr TypeIndex(uint32_t idx, TypeCategory cat) noexcept
        : index_(idx), category_(cat) {}

    // Cheap sentinel: "was this TypeIndex ever set?"
    constexpr bool isNull() const noexcept {
        return index_ == 0 && category_ == TypeCategory::Invalid;
    }
    // Runtime bounds check: "can I call getTypeInfo() safely?"
    // Replaces scattered idx.value < gTypeInfo.size() checks.
    bool isValid() const noexcept;  // defined in AstNodeTypes.cpp

    // Classification — no gTypeInfo lookup needed.
    constexpr TypeCategory category()   const noexcept { return category_; }
    constexpr bool isStruct()           const noexcept { return category_ == TypeCategory::Struct; }
    constexpr bool isEnum()             const noexcept { return category_ == TypeCategory::Enum; }
    constexpr bool isTypeAlias()        const noexcept { return category_ == TypeCategory::TypeAlias; }
    constexpr bool isStructLike()       const noexcept {
        return category_ == TypeCategory::Struct
            || category_ == TypeCategory::UserDefined
            || category_ == TypeCategory::TypeAlias;
    }
    constexpr bool isPrimitive()        const noexcept;  // switch on category_
    constexpr bool isTemplatePlaceholder() const noexcept {
        return category_ == TypeCategory::Template;
    }

    // Identity: both index and category must match.
    constexpr auto operator<=>(const TypeIndex&) const noexcept = default;
};
// Size: uint32_t(4) + uint8_t(1) + 3 padding = 8 bytes — same as current size_t TypeIndex.
```

Note: the current `is_valid()` method (sentinel check: `value > 0`) maps to `!isNull()` in Option D. The new `isValid()` is a stricter runtime bounds check. Every call site needs to understand which semantics it needs — this rename has wide impact (see §5 Risk 4 below).

### Proposed `gTypeInfo` accessor API

No code outside `AstNodeTypes.cpp` touches `gTypeInfo` directly:

```cpp
// --- Reads (declared in AstNodeTypes.h, defined in AstNodeTypes.cpp) ---
const TypeInfo& getTypeInfo(TypeIndex idx);       // asserts in range
TypeInfo&       getTypeInfoMut(TypeIndex idx);
const TypeInfo* findTypeByName(StringHandle name);   // replaces gTypesByName access
const TypeInfo* findNativeType(TypeCategory cat);    // replaces gNativeTypes access

// --- Writes ---
struct TypeCreationResult {
    TypeInfo& info;
    TypeIndex index;
};
TypeCreationResult addStructType(StringHandle name, NamespaceHandle ns = {});
TypeCreationResult addEnumType  (StringHandle name, NamespaceHandle ns = {});
TypeCreationResult addUserType  (StringHandle name, int size_bits = 0, NamespaceHandle ns = {});
TypeCreationResult addFunctionType(StringHandle name, NamespaceHandle ns = {});
TypeCreationResult registerTypeAlias(StringHandle name, const TypeSpecifierNode& spec, NamespaceHandle ns = {});
TypeCreationResult addNativeType(StringHandle name, TypeCategory cat, int size_bits);
```

The key payoff: callers never compute `TypeIndex{gTypeInfo.size(), ...}` manually — the `add*` functions do it and return the result. This eliminates the most error-prone antipattern in the current codebase.

### Migration strategy: 4 steps

**Step 0 — Create the accessor boundary (zero semantic risk, scriptable)**

Replace all direct `gTypeInfo` accesses with function calls. This step is pure mechanical refactoring with no behavioral change:

- `gTypeInfo[x.value]` → `getTypeInfo(x)` (const context) / `getTypeInfoMut(x)` (mutable context)
- `x.value < gTypeInfo.size()` → `x.isValid()`
- `x.value >= gTypeInfo.size()` → `!x.isValid()`
- Direct `gTypesByName[name]` → `findTypeByName(name)`
- Direct `gNativeTypes[type]` → `findNativeType(TypeCategory::X)`
- `TypeIndex{gTypeInfo.size()}` → replaced by `addXxx(...).index` at write sites

Current scope: **1,624** external `gTypeInfo` accesses (outside `AstNodeTypes.cpp`), **455** `.value < gTypeInfo.size()` checks. A Python migration script is realistic for the bulk replacement; manual review is needed for the ~30 `TypeIndex{gTypeInfo.size()}` construction sites.

**Step 1 — Add `TypeCategory` and embed it in `TypeIndex`**

- Define `TypeCategory` with the explicit values shown above.
- Change `TypeIndex` layout from `{ size_t value; }` to `{ uint32_t index_; TypeCategory category_; }`.
- Update `getTypeInfo()` to use `idx.index_` instead of `idx.value`.
- Update all `add*` / `addNativeType` functions to construct `TypeIndex` with the correct category.
- Update `std::hash<TypeIndex>` and `std::formatter<TypeIndex>` specializations.
- Keep `Type` enum untouched — it still compiles, still works.

**Step 2 — Add `TypeCategory`-based classification helpers**

Mirror every `Type`-based helper with a `TypeCategory` version:

```cpp
constexpr bool is_primitive_type(TypeCategory cat);
constexpr bool isArithmeticType(TypeCategory cat);
constexpr bool isFundamentalType(TypeCategory cat);
constexpr bool is_builtin_type(TypeCategory cat);
// etc.
```

And add the convenience methods on `TypeIndex` itself (`isPrimitive()`, `isStruct()`, etc. — see struct above).

**Step 3 — Incrementally replace `Type` usage with `TypeCategory`/`TypeIndex` queries**

Because `TypeCategory ≠ Type`, the compiler flags every remaining old-enum site. Migrate file by file:

- `type == Type::Struct` → `idx.isStruct()`
- `type == Type::Enum` → `idx.isEnum()`
- Fields carrying `Type` alongside `TypeIndex` (e.g., `TemplateTypeArg::base_type`) → remove the field, use `idx.category()` instead
- `buildConversionPlan(Type, Type)` → add a `buildConversionPlan(TypeCategory, TypeCategory)` overload, migrate callers one by one (see §7.2)
- `gNativeTypes` (keyed on `Type`) → re-key on `TypeCategory` using `findNativeType(cat)`

Once all references to `Type::` are gone, delete the old `Type` enum.

### What Option D solves that Option C leaves open

| Problem | Option C | Option D |
| --- | --- | --- |
| `Type` + `TypeIndex` drifting out of sync | Mitigated by `TypeInfo` helpers | **Eliminated**: `TypeIndex` is the only token |
| `Type::UserDefined` ambiguity (§7.1) | Requires `TypeInfo::is_type_alias` flag | **Built-in**: `TypeCategory::TypeAlias` |
| `gTypeInfo` scattered throughout call sites | Unchanged | **Hidden**: accessor API |
| `TypeIndex{gTypeInfo.size()}` error-prone pattern | Unchanged (455 sites) | **Eliminated**: `add*()` returns `TypeCreationResult` |
| Compiler-enforced migration tracking | Manual grep | **Automatic**: `TypeCategory ≠ Type` |
| `isArithmeticType` without gTypeInfo lookup | Partial (current) | **Full**: `idx.isPrimitive()` etc. |

### Evaluation

**Strengths of Option D:**

1. **Architecturally cleaner than Option C** — Single identity token, no parallel fields to sync, no hidden `gTypeInfo` access patterns.

2. **Solves §7.1 structurally** — `TypeCategory::TypeAlias` vs `TypeCategory::UserDefined` from day one. The ambiguity that §7.1 flags as the single most important issue is resolved by the enum definition, not by documentation or runtime flags.

3. **Compiler-enforced migration** — Every remaining `Type::` site is a compile error after `TypeCategory` is in use. This is far more reliable than grep-based tracking.

4. **`TypeCreationResult` pattern eliminates the biggest antipattern** — The `TypeIndex{gTypeInfo.size()}` expression appears in ~30 files. Under Option D, callers never compute this themselves; they receive the index from the `add*` function.

5. **Log-comparable numeric values** — Values 0-23 match the old `Type` enum. A debug print showing `category=11` means `Int` in both old and new systems.

**Risks and complications:**

1. **`TemplateTypeArg::base_type` is `Type`** — `TemplateTypeArg` (`src/TemplateRegistry_Types.h:157`) carries a `Type base_type` field alongside `TypeIndex type_index`. This is a heavily used structure (122+ write sites). Under Option D, `base_type` would eventually be replaced by reading `type_index.category()`, but this field is also used for primitive types where `type_index` may be invalid/zero. The migration here requires care.

2. **`TypeInfo::TemplateArgInfo::base_type` is also `Type`** — The lightweight template arg storage in `TypeInfo` (`src/AstNodeTypes_DeclNodes.h:769`) has the same dual-field pattern. Same migration challenge.

3. **`gNativeTypes` key type** — `gNativeTypes` is `std::unordered_map<Type, const TypeInfo*>` (`src/AstNodeTypes.cpp:72`). It would need to become `unordered_map<TypeCategory, const TypeInfo*>` or be replaced entirely by the `findNativeType(cat)` accessor.

4. **`is_valid()` rename** — The current `TypeIndex::is_valid()` returns `value > 0` (a sentinel/null check). The proposal renames this to `isNull()` and defines a new `isValid()` as a runtime bounds check. This rename touches every one of the 455+ sites that currently call `.is_valid()`. Many of those sites really want the sentinel check, not the bounds check — careful inspection is needed at each site.

5. **`TypeCategory::TypeAlias = 24` shifts values ≥ 24 by 1** — Any code comparing `TypeCategory` values numerically (including serialization, debug output, or hardcoded switch cases without explicit labels) would silently get wrong results. The `static_assert` correspondence checks in the proposal help, but all switch statements need review.

6. **Step 0 scope is large** — 1,624 external `gTypeInfo` accesses outside `AstNodeTypes.cpp` is a very large Step 0. A Python migration script is necessary, not optional. The script needs to handle const vs mutable contexts correctly (`getTypeInfo` vs `getTypeInfoMut`), and the hand-off between the script output and human review should be planned.

7. **`TypeIndex` increment operators** — The current `TypeIndex` has `operator++` used in loop variables that walk the type table. After the layout change, `++idx` would increment `index_` (not `category_`), which is correct. But the increment operators still use `++value` internally and would need updating to `++index_`.

### Option D vs Option C: decision guidance

Option D is **architecturally superior** to Option C for the long run. Option C is a safe incremental path, but it intentionally avoids the hard problems (dual field, `gTypeInfo` encapsulation, `UserDefined` ambiguity). Option D addresses all three.

**Option D is the right final destination. Option C is a valid staging area on the way there.**

The practical recommendation is:

- **Continue with Option C Milestones 2-3** (add `TypeInfo` helpers, migrate hot spots) — these make Option D Step 1 cheaper because they reduce the number of sites that need simultaneous migration.
- **Execute Option D Step 0** (accessor API) as a separate PR once Milestone 2 is done. This is the highest-value, lowest-risk change in the whole roadmap: it does not change behavior but encapsulates `gTypeInfo` so future layout changes only touch the accessor implementations.
- **Then execute Option D Steps 1-3** after Step 0 is merged. At that point the codebase is already accessor-only, the `TypeInfo` helpers exist, and the `TypeCategory` introduction is a much smaller diff.

---

## 6. Recommendation and migration plan

### Recommended path: **Option C now → Option D Step 0 → Option D Steps 1-3**

Option A is done. The remaining milestones below follow Option C until the accessor boundary is established, then switch to Option D for the full unification. See §5 for the full Option D evaluation.

The updated recommendation relative to the previous version of this document:
- **Milestones 1-3 below** are still Option C work and remain the right immediate path.
- **Milestone 4 (new)** is Option D Step 0: create the `gTypeInfo` accessor API as a standalone, behavior-preserving PR. This gates all subsequent Option D work.
- **Milestone 5+ (new)** introduces `TypeCategory`, embeds it in `TypeIndex`, and executes the full unification.

#### Milestone 1 — Complete Option A remaining TODOs (low risk, high value)

- [x] **TODO 1**: Add `is_builtin_type(Type)` helper and replace the two range checks in `Parser_Core.cpp:65` and `Parser_Templates_Inst_Deduction.cpp:32`.
- [x] **TODO 2**: Move `isArithmeticType` and `isFundamentalType` to `AstNodeTypes_TypeSystem.h` as `constexpr` switch-based helpers. Delete the three duplicated local copies.
- [x] **TODO 3**: Consolidate `is_integral_type` / `isIntegralType` to one definition in `AstNodeTypes_TypeSystem.h`. Removed `is_integral_type` from `OverloadResolution.h`; all 5 call sites now use `isIntegralType`.

#### Milestone 2 — Add `TypeInfo` query helpers and resolve `UserDefined` ambiguity (Option C Step A)

- [x] Add `isStructLike()`, `isPrimitive()`, `needsTypeIndex()`, `isTemplatePlaceholder()` to `TypeInfo` in `AstNodeTypes_DeclNodes.h`.
- [ ] Replace the most redundant `gTypeInfo[idx].type_ == Type::X` patterns with the new helpers. Priority: `TemplateRegistry_Types.h` and `OverloadResolution.h`.
- [x] Evaluate splitting `Type::UserDefined` into `TypeAlias` + `UserDefined`, or adding an `is_type_alias` flag to `TypeInfo` (see §7.1). Decision: add `is_type_alias_` flag and `isTypeAlias()` method; set in `register_type_alias`. Defer full split to Milestone 7 (TypeCategory).
- [x] Add a `static_assert` enum-count sentinel to catch new `Type` values (see §7.3). Added `Type::Count_` and `static_assert(static_cast<int>(Type::Count_) == 31, ...)`.

#### Milestone 2.5 — Resolve the `Type::UserDefined` ambiguity (prerequisite for Milestone 3)

- [x] Decide between option (a) split `UserDefined` into `TypeAlias`/`OpaqueUserType`, option (b) make `resolve_type_alias()` recursive, or option (c) accept and document. Decision: add `is_type_alias_` flag to `TypeInfo` (intermediate step toward option (a) TypeAlias split in Milestone 7) — `register_type_alias` sets it; `add_user_type` does not.
- [ ] If option (b): add cycle detection (max-depth guard) and update `resolve_type_alias()` to follow `UserDefined` → `UserDefined` chains. Verify that `buildConversionPlan` still passes all overload resolution tests.
- [ ] If option (a): define the new enum variants, update `is_struct_type()` and `needs_type_index()`, and migrate call sites incrementally.

#### Milestone 3 — Migrate mixed `Type` + `TypeIndex` hot spots (Option C Step B/C)

- [ ] Convert the mixed-identity sites in `ExpressionSubstitutor.cpp` and `SemanticAnalysis.cpp` to use `TypeIndex` as primary, reading category from `TypeInfo` methods.
- [ ] Replace the alias-resolution pattern in `AstNodeTypes_DeclNodes.h:837-845` with explicit `TypeInfo` helper calls.
- [ ] **Sub-milestone 3a**: Migrate `buildConversionPlan` separately with dedicated test coverage (see §7.2). Do not batch this with mechanical helper replacements.
- [ ] Upgrade `resolve_type_alias()` to chase alias chains (bounded depth) or document why the shallow version is intentional (see §7.1 option b).
- [ ] Document `buildConversionPlan` as a legitimate `Type`-primary consumer — do not attempt to migrate its core dispatch to `TypeIndex` (see §7.2).

#### Milestone 4 — Settle `Type::Template` (Option C Step D)

- [x] Add `TypeInfo::isTemplatePlaceholder()` and replace the 6 raw `type_ == Type::Template` comparisons with the helper call. Added to `TypeInfo` in `AstNodeTypes_DeclNodes.h`.
- [x] Document in code why `Type::Template` is not part of `needs_type_index()`. Added comment to `needs_type_index()` in `AstNodeTypes_TypeSystem.h`.

#### Milestone 5 — Audit and narrow remaining raw `Type` consumers

- [ ] Audit `NameMangling.h`, `IrType.cpp`, and codegen `!= Type::Struct` single-type checks.
- [ ] Decide whether `Type` should be narrowed to a smaller `TypeCategory` enum covering only the codegen-relevant categories, or left as-is with clear documentation that it is a cache, not an identity system.

#### Milestone 6 (Option D Step 0) — Create `gTypeInfo` accessor API

This is a behavior-preserving PR that establishes the encapsulation boundary for Option D. No semantic changes; the entire value is decoupling call sites from `gTypeInfo` internals.

**Scope**: 1,624 external `gTypeInfo` accesses, 455 `.value < gTypeInfo.size()` checks, ~30 `TypeIndex{gTypeInfo.size()}` construction sites.

- [x] Add `getTypeInfo(TypeIndex)`, `getTypeInfoMut(TypeIndex)`, `findTypeByName()`, `findNativeType()`, `getTypeInfoCount()`, `forEachTypeInfo<Fn>()` in `AstNodeTypes_DeclNodes.h` / `AstNodeTypes.cpp`.
- [x] Add `add_template_param_type()`, `add_instantiated_type()`, `add_type_alias_copy()`, `add_empty_type_entry()` helpers for sites that previously called `gTypeInfo.emplace_back()` directly outside `AstNodeTypes.cpp`.
- [x] Replace all external `gTypeInfo[x.value]` reads with `getTypeInfo(x)`, mutable accesses with `getTypeInfoMut(x)`, `gTypeInfo.size()` with `getTypeInfoCount()`.
- [x] Replace external `gTypesByName` access with `findTypeByName()` / `getTypesByNameMap()`, external `gNativeTypes` access with `findNativeType()` / `getNativeTypesMap()`.
- [x] Remove `extern` declarations of `gTypeInfo`, `gTypesByName`, `gNativeTypes` from `AstNodeTypes_DeclNodes.h` so no new code can access them directly.
- [ ] Change `add_struct_type`, `add_enum_type`, `add_user_type`, `register_type_alias` to return `TypeCreationResult {TypeInfo& info; TypeIndex index;}` (deferred to Milestone 7).

#### Milestone 7 (Option D Steps 1-3) — Introduce `TypeCategory` and unify identity

These steps can be split into separate PRs once Milestone 6 is merged.

- [x] Add `TypeCategory` enum with explicit values (§5); add `static_assert` correspondence checks with old `Type` values.
- [x] Embed `TypeCategory category_` in `TypeIndex` alongside `uint32_t value`. The `value` field is kept (not renamed to `index_`) for backward compatibility during migration; comparison operators are updated to compare only `.value` so that legacy `TypeIndex{n}` constructions remain correct. Update `std::hash<TypeIndex>` and `std::formatter<TypeIndex>` to use `uint32_t`. Keep `Type` untouched.
- [x] Update all `add*` / `initialize_native_types` to pass the correct `TypeCategory` when constructing `TypeIndex`. `register_type_alias` sets the alias TypeInfo via the existing `is_type_alias_` flag; the self-`TypeIndex` carrying `TypeCategory::TypeAlias` will be introduced with `TypeCreationResult` below.
- [x] Add `TypeCategory`-based classification helpers (`is_primitive_type`, `is_struct_type`, `needs_type_index`, `is_builtin_type`, `isArithmeticType`, `isFundamentalType`, `isIntegralType`, `isFloatingPointType`) mirroring the existing `Type`-based helpers.
- [x] Add `typeToCategory(Type)` helper to convert legacy `Type` values to `TypeCategory`.
- [x] Add classification methods directly on `TypeIndex` (`isStruct`, `isEnum`, `isTypeAlias`, `isPrimitive`, `isStructLike`, `needsTypeIndex`, `isTemplatePlaceholder`, `isFunction`, `isNull`, `category`).
- [ ] Change `add_struct_type`, `add_enum_type`, `add_user_type`, `register_type_alias` to return `TypeCreationResult {TypeInfo& info; TypeIndex index;}` (deferred from Milestone 6). This enables callers to receive a `TypeIndex{pos, TypeCategory::TypeAlias}` for aliases without computing `gTypeInfo.size()-1` themselves.
- [ ] Rename `TypeIndex::value` → `TypeIndex::index_` at all ~499 call sites now that the layout is established. Update comparison operators and accessors accordingly.
- [ ] Migrate `TemplateTypeArg::base_type` (122+ write sites) from `Type` to relying on `type_index.category()`.
- [ ] Migrate `TypeInfo::TemplateArgInfo::base_type` similarly.
- [ ] Re-key `gNativeTypes` from `Type` to `TypeCategory`.
- [ ] Migrate remaining `Type::` sites to `TypeCategory::`/`TypeIndex` queries file by file. Use compile errors to track remaining sites.
- [ ] Delete the `Type` enum once all references are gone.

### `Type::Template` decision (immediate)

Do **not** fold `Type::Template` into `needs_type_index()` yet. Template placeholders are semantically different from concrete `Struct` / `Enum` / `UserDefined` types and require different handling during substitution. Adding `isTemplatePlaceholder()` to `TypeInfo` (Milestone 4) is the right way to make this boundary explicit.

---

## 7. Known risks and gaps in the Option C roadmap

This section captures structural issues that the milestones above do not fully address.
They should be evaluated before committing to Milestones 3–5.

### 7.1 The `Type::UserDefined` semantic ambiguity

**This is the single most important architectural issue in the type system, and the
current roadmap underweights it.**

`Type::UserDefined` is used for two fundamentally different things:

1. **Resolved typedef aliases** — e.g., `using size_t = unsigned long long;` creates a
   `TypeInfo` with `type_ == Type::UserDefined` and a `type_index_` pointing to the
   underlying type. The `resolve_type_alias()` function in `src/OverloadResolution.h:180-188`
   handles this case, but only resolves to primitives — if the underlying type is another
   `UserDefined`, `Struct`, or `Enum`, it gives up and returns `Type::UserDefined`.

2. **Unresolved or opaque user types** — e.g., `__builtin_va_list` is registered as
   `Type::UserDefined` in `src/AstNodeTypes.cpp:214`. Template parameter types that
   haven't been substituted yet may also appear as `UserDefined`.

The overload resolution code in `src/OverloadResolution.h` has multiple workarounds for
this ambiguity:

- Lines 636–647 (in the `TypeSpecifierNode` overload of `buildConversionPlan`): when a
  type is still `UserDefined` with `type_index == 0` after alias resolution, the code
  optimistically allows conversion to/from integral types, assuming it's probably
  `size_t` or `ptrdiff_t`.
- Lines 432–438 (pointer conversion): when either pointer base type is still
  `UserDefined` after resolution, the code accepts a `PointerConversion` rather than
  rejecting the match, because the `UserDefined` might be a template parameter that
  resolves to a compatible type.
- Lines 600–604 (reference-from-non-reference): same optimistic pattern for
  `UserDefined` after alias resolution.

These are not bugs today — they're pragmatic workarounds. But they mean that
`is_struct_type()` returning `true` for `UserDefined` is technically a lie in some
contexts: a `UserDefined` might actually be an alias for `int`, not a struct at all.

**Recommendation**: Before Milestone 3, decide whether to:

- **(a)** Split `Type::UserDefined` into `Type::TypeAlias` (resolved, points to underlying
  type) and `Type::OpaqueUserType` (unresolved, used for builtins and unsubstituted
  template params). This is the cleanest fix but touches many call sites.
- **(b)** Make `resolve_type_alias()` recursive (follow chains of `UserDefined` →
  `UserDefined` → ... → concrete type) and always resolve before classification. This
  is less invasive but requires care to avoid infinite loops (alias cycles).
- **(c)** Accept the ambiguity and document it. The current workarounds work in practice;
  the cost is occasional optimistic conversion acceptance that CodeGen later rejects.

Option (b) is probably the best cost/benefit for the near term. Option (a) is the right
long-term answer but should be its own milestone.

### 7.2 `buildConversionPlan` is deeply entangled with raw `Type` comparisons

Milestone 3 says "convert mixed `Type` + `TypeIndex` hot spots in `OverloadResolution.h`"
but understates the difficulty. `buildConversionPlan` (both the `Type,Type` overload at
`src/OverloadResolution.h:82-186` and the `TypeSpecifierNode,TypeSpecifierNode` overload
at `src/OverloadResolution.h:353-669`) is the hottest path in the compiler for type
classification, and it interleaves `Type` enum comparisons with `TypeIndex` identity
checks at every level:

- Enum-to-int promotion (`from == Type::Enum && to == Type::Int`)
- Struct-to-struct identity (`from_type == Type::Struct && to_type == Type::Struct &&
  from.type_index() == to.type_index()`)
- User-defined conversion detection (`from == Type::Struct && to != Type::Struct`)
- Pointer-to-pointer compatibility (resolve aliases, then compare `Type` and `TypeIndex`)

These are not mechanical replacements — each branch encodes a specific C++20 conversion
rule. Migrating them to `TypeInfo` query helpers requires understanding the conversion
semantics, not just the classification API. This should be treated as a separate,
carefully tested milestone rather than part of a bulk migration.

**Recommendation**: Add a dedicated sub-milestone under Milestone 3 for
`buildConversionPlan` specifically, with its own test plan covering:
- Primitive-to-primitive promotions and conversions
- Struct-to-struct identity (same TypeIndex vs different TypeIndex)
- UserDefined alias resolution through pointer and reference layers
- Enum-to-integral promotion paths

### 7.3 Enum exhaustiveness is not enforced for classification helpers

The `constexpr` switch-based helpers (`is_primitive_type`, `is_builtin_type`,
`isArithmeticType`, `needs_type_index`) use `default: return false;`. This means that
if a new `Type` enum variant is added (e.g., `Type::Union`, `Type::Concept`,
`Type::ConstrainedAuto`), the helpers silently return `false` instead of producing a
compile error.

The `static_assert` checks in `src/AstNodeTypes_TypeSystem.h:368-381` only verify
known positive/negative cases — they cannot catch a newly added variant that should
be in a helper but isn't.

Some existing functions in the codebase *do* enumerate all cases explicitly (e.g.,
`isSignedType` at `src/AstNodeTypes_TypeSystem.h:617-657` lists every `Type` variant
including a `default` that covers `Type::Template`). These would produce `-Wswitch`
warnings if a new variant were added. The classification helpers do not have this
property.

**Recommendation**: For each classification helper, add a comment documenting which
`Type` variants are intentionally excluded and why. Consider adding a compile-time
enum count check (e.g., `static_assert` on the number of `Type` variants) that forces
a manual audit of all helpers whenever the enum grows. Alternatively, periodically
verify that the helpers' positive + negative cases cover all enum values.

### 7.4 `is_integral_type` vs `isIntegralType` — semantically identical, safe to merge

The document's TODO 3 says "consolidate" but doesn't confirm whether the two functions
have identical semantics. They do:

- `isIntegralType(Type)` at `src/AstNodeTypes_TypeSystem.h:461-482` includes `Bool` and
  all integer/char types via an explicit switch.
- `is_integral_type(Type)` at `src/OverloadResolution.h:46-48` is defined as
  `type == Type::Bool || is_integer_type(type)`, where `is_integer_type` at
  `src/AstNodeTypes.cpp:222-242` covers all integer/char types *excluding* Bool.

So `is_integral_type(x)` ≡ `isIntegralType(x)` for all `Type` values. The merge is
safe: replace all `is_integral_type` call sites (5 in `src/OverloadResolution.h`) with
`isIntegralType`, then delete the `is_integral_type` definition.

### 7.5 Global mutable state (`gTypeInfo`) constrains future parallelism

`gTypeInfo` is a global `std::deque<TypeInfo>` (`src/AstNodeTypes.cpp:70`), and
`gTypesByName` is a global `std::unordered_map` (`src/AstNodeTypes.cpp:71`). The
Option C plan proposes making `TypeIndex` the primary identity and reading category
from `TypeInfo` methods — which means more code paths will dereference into `gTypeInfo`.

This is fine for a single-threaded compiler, but if parallel compilation units or
concurrent template instantiation are ever needed, the global mutable state becomes a
bottleneck. The document should acknowledge this constraint so that future work doesn't
accidentally make the coupling worse.

**Recommendation**: This is not a blocker for Option C, but any new `TypeInfo` query
helpers should be `const` methods that read only from the `TypeInfo` instance, not from
global state. This keeps the door open for a future where `TypeInfo` objects are owned
per-compilation-unit rather than globally.


