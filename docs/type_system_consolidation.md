# Type system consolidation audit and design options (2026-03-25)

This document records the Phase 1 audit and evaluates the requested consolidation options.

> Audit scope: `src/`
>
> Audit baseline: pre-consolidation tree at commit `8a0f990` (the commit immediately before the raw enum checks were replaced with helpers in this branch). The code changes in this branch intentionally keep semantics the same; the counts below therefore describe the original scattered patterns that motivated the cleanup.

## 1. Current State Analysis

### Summary

The current semantic type model is split across two identifiers:

- `Type` answers fast category questions (`Struct`, `Enum`, `Int`, `Template`, ...).
- `TypeIndex` identifies concrete entries in `gTypeInfo`.

That split works, but the audit shows that the codebase had accumulated multiple ad-hoc ways of asking the same questions:

- **8 recurring ad-hoc pattern families** around `Struct` / `UserDefined` / `Enum` / `Template`
- **77 redundant helper-equivalent checks** for the existing `is_struct_type()` concept (`59` positive + `18` negative)
- **9 call sites** using the broader `Struct || Enum || UserDefined` identity-carrying classification (`8` positive + `1` negative)
- **4 template-placeholder-specific sites** that already treat `Type::Template` as being in the same neighborhood as `UserDefined`
- **4 range-based enum-order-dependent sites** (`Parser_Core.cpp`, `Parser_Templates_Inst_Deduction.cpp`, `TypeTraitEvaluator.cpp`, `IrGenerator_MemberAccess.cpp`)

The exact redundant anti-pattern called out in the task (`!is_struct_type(x) && x != Type::UserDefined`) did **not** exist verbatim in the audited tree. The redundancy showed up in the opposite form instead: dozens of sites were open-coding `x == Type::Struct || x == Type::UserDefined` or its negation instead of calling `is_struct_type()`.

### Distinct patterns found

| Pattern family | Count | Representative locations | Notes |
| --- | ---: | --- | --- |
| `x == Type::Struct || x == Type::UserDefined` (or reversed) | 59 | `src/ConstExprEvaluator_Core.cpp:1369`, `src/Parser_Expr_PostfixCalls.cpp:1285`, `src/Parser_Templates_Inst_ClassTemplate.cpp:1187`, `src/IrGenerator_Stmt_Decl.cpp:531` | Redundant with `is_struct_type()` (`src/AstNodeTypes.cpp:259-260`) |
| `x != Type::Struct && x != Type::UserDefined` (or reversed) | 18 | `src/ConstExprEvaluator_Members.cpp:3388`, `src/Parser_Expr_PostfixCalls.cpp:21`, `src/Parser_Statements.cpp:1987` | Also redundant with `is_struct_type()` |
| `x == Type::Struct || x == Type::Enum || x == Type::UserDefined` | 8 | `src/TemplateRegistry_Types.h:293`, `src/AstNodeTypes_DeclNodes.h:1174`, `src/OverloadResolution.h:1259`, `src/ConstExprEvaluator_Members.cpp:2072` | This is the codebase's de-facto "carries semantic identity via `TypeIndex`" check |
| `x != Type::Struct && x != Type::Enum && x != Type::UserDefined` | 1 | `src/AstNodeTypes_DeclNodes.h:840` | Logical negation of the previous row |
| `x == Type::Template` | 6 | `src/TemplateInstantiationHelper.h:305`, `src/TemplateInstantiationHelper.h:334`, `src/ExpressionSubstitutor.cpp:185` | Shows unresolved template params are still identified through raw `Type` |
| `x == Type::Template || x == Type::UserDefined` | 3 | `src/ExpressionSubstitutor.cpp:661`, `src/Parser_Templates_Inst_Substitution.cpp:614` | Strong signal that `Template` already behaves like a sibling of `UserDefined` in some flows |
| `x == Type::Template || isPlaceholderAutoType(x) || x == Type::UserDefined` | 1 | `src/ExpressionSubstitutor.cpp:981-982` | The broadest unresolved-type placeholder check in the tree |
| `arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer` | 2 | `src/Parser_Core.cpp:65`, `src/Parser_Templates_Inst_Deduction.cpp:32` | Enum-order-dependent primitive/pointer classification |
| `Type::Bool <= x <= Type::LongDouble` inside helper functions | 2 | `src/TypeTraitEvaluator.cpp:5-13`, `src/IrGenerator_MemberAccess.cpp:2119-2129` | Enum-order-dependent arithmetic/fundamental classification |

### Inconsistencies

The main inconsistencies are structural, not stylistic:

1. **`Struct || UserDefined` vs `Struct || Enum || UserDefined`**
   - Struct-like object paths use the narrower two-way check.
   - Identity/hash/equality paths often use the three-way check.
   - Examples: `src/ConstExprEvaluator_Core.cpp:1369` vs `src/TemplateRegistry_Types.h:293` and `src/OverloadResolution.h:1259`.

2. **`Template` is sometimes treated like `UserDefined`, but not systematically**
   - `ExpressionSubstitutor` explicitly groups `Template` with `UserDefined` (`src/ExpressionSubstitutor.cpp:661`, `src/ExpressionSubstitutor.cpp:981-982`).
   - `TemplateInstantiationHelper` still checks `Type::Template` directly (`src/TemplateInstantiationHelper.h:305`, `src/TemplateInstantiationHelper.h:334`).
   - The rest of the semantic layer usually ignores `Template` in identity-classification helpers.

3. **Some code consults both `Type` and `TypeIndex`, some only `Type`**
   - Mixed `Type` + `TypeIndex`: `src/AstNodeTypes_DeclNodes.h:837-845`, `src/AstNodeTypes_DeclNodes.h:1174-1185`, `src/TemplateRegistry_Types.h:290-320`, `src/OverloadResolution.h:1262-1303`, `src/IrGenerator_Expr_Operators.cpp:1392-1401`.
   - `Type` alone: `src/NameMangling.h:174-186`, `src/TypeTraitEvaluator.cpp:5-13`, `src/IrType.cpp`, `src/Parser_Core.cpp:65`, `src/SemanticAnalysis.cpp:2968-2972`.

### Redundancy

The biggest redundancy is not incorrect logic; it is repeated logic:

- `is_struct_type()` already exists at `src/AstNodeTypes.cpp:259-260`.
- The audit still found **77** open-coded two-way `Struct/UserDefined` checks.
- The codebase also already had a near-`needs_type_index()` concept in two separate places:
  - `src/IrType.h:72-86` (`carriesSemanticTypeIndex`)
  - `src/OverloadResolution.h:1258-1292` (`binaryOperatorUsesTypeIndexIdentity`)

### Existing helper functions: coverage vs gaps

| Helper | Location | Covers | Misses / limitation |
| --- | --- | --- | --- |
| `is_struct_type(Type)` | `src/AstNodeTypes.cpp:259-260` | `Struct`, `UserDefined` | Excludes `Enum` and `Template`; underused before this branch |
| `is_integer_type(Type)` | `src/AstNodeTypes.cpp:222-242` | integer primitives | Not `Bool`, not floats, not `Void`, not `Nullptr`, not identity-carrying user types |
| `is_floating_point_type(Type)` | `src/AstNodeTypes.cpp:248-257` | `Float`, `Double`, `LongDouble` | No integral/fundamental/identity classification |
| `is_integral_type(Type)` | `src/OverloadResolution.h:45-47` | `Bool` + integer primitives | No floating-point/fundamental/identity classification |
| `isArithmeticType(Type)` | `src/TypeTraitEvaluator.cpp:5-9`, `src/IrGenerator_MemberAccess.cpp:2119-2124` | arithmetic family | Depends on enum ordering (`Bool..LongDouble`) |
| `isFundamentalType(Type)` | `src/TypeTraitEvaluator.cpp:11-13`, `src/IrGenerator_MemberAccess.cpp:2126-2129` | `Void`, `Nullptr`, arithmetic | Also depends on enum ordering |
| `binaryOperatorUsesTypeIndexIdentity(Type)` | `src/OverloadResolution.h:1258-1292` | `Struct`, `Enum`, `UserDefined` | Only exists inside overload-resolution logic |

### Places where `Type` and `TypeIndex` are used together

These are the key sites that already model types as a pair rather than as `Type` alone:

- **Alias/size resolution**: `src/AstNodeTypes_DeclNodes.h:837-845`, `src/AstNodeTypes_DeclNodes.h:1174-1185`
- **Template hashing/equality**: `src/TemplateRegistry_Types.h:290-320`, `src/TemplateRegistry_Types.h:455-493`
- **Binary operator operand identity**: `src/OverloadResolution.h:1262-1303`, `src/IrGenerator_Expr_Operators.cpp:1392-1401`
- **Substitution and template lookup**: `src/ExpressionSubstitutor.cpp:981-984`, `src/Parser_Templates_Inst_Substitution.cpp:612-614`

These are the most natural future migration points if `TypeIndex` becomes primary.

### Places that rely on `Type` alone and would break if `Type` disappeared

Representative examples:

- **Name mangling**: `src/NameMangling.h:174-186`
- **Type-trait arithmetic/fundamental checks**: `src/TypeTraitEvaluator.cpp:5-13`
- **IR-lowering category mapping**: `src/IrType.cpp`
- **Parser-side primitive-range checks**: `src/Parser_Core.cpp:65`, `src/Parser_Templates_Inst_Deduction.cpp:32`
- **Raw semantic branching**: `src/SemanticAnalysis.cpp:2968-2972`, `src/TemplateInstantiationHelper.h:305-334`

A pure TypeIndex migration would therefore be much broader than just replacing equality checks.

## 2. Option A: Keep `Type`, add helper functions

### Proposal

Keep the current dual system, but make the classification API explicit:

- `is_primitive_type(Type)` — true for `Void` through `LongDouble`, plus `Nullptr`
- `needs_type_index(Type)` — true for `Struct`, `Enum`, and `UserDefined`
- Keep `is_struct_type(Type)` as the narrow "struct/class-like object" test

This branch already implements that minimal cleanup and uses the helpers to remove raw duplication.

### Estimated call-site impact

The current cleanup replaced **76 raw checks across 25 files**, which is a good proxy for the real Option A footprint. A full pass including any remaining intentional single-type branches would likely stay in the **75-90 call-site** range rather than requiring an architectural rewrite.

### Pros

- Smallest, safest change
- Backward compatible with the current AST, parser, semantic, and mangling code
- Easy to review and test incrementally
- Immediately removes the most repetitive `Struct/UserDefined` disjunctions

### Cons

- Keeps **two parallel identification systems** (`Type` and `TypeIndex`)
- Callers can still ask the wrong question (`is_struct_type` vs `needs_type_index`)
- Does not solve enum-order-dependent helpers
- Does not answer the long-term `Type::Template` question

## 3. Option B: Extend `TypeIndex` to replace `Type`

### Sketch

A full replacement would make `TypeIndex` the only type identifier carried through the semantic layer. There are two viable shapes:

1. **Packed tagged value**
   - e.g. `uint32_t payload`
   - low bits encode category/flags (`primitive`, `enum`, `template placeholder`, `pointer-like`, ...)
   - remaining bits encode either a primitive id or an index into `gTypeInfo`

2. **Reserved-index scheme**
   - reserve a fixed range of `TypeIndex` values for primitive/builtin kinds
   - all user types continue to live in `gTypeInfo`
   - helpers become `type_index.isPrimitive()`, `type_index.primitiveKind()`, etc.

### Primitive registration and constexpr feasibility

`initialize_native_types()` already registers all builtins in `gTypeInfo` (`src/AstNodeTypes.cpp:133-218`). That means the raw data already exists in one place, but the current primitive `TypeIndex` values are assigned **at runtime** via `gTypeInfo.emplace_back(...)` and are therefore **not `constexpr` today**.

To make primitive `TypeIndex` values constexpr, one of the following would have to change:

- reserve hard-coded primitive indices and guarantee them before `initialize_native_types()` runs, or
- move builtin type metadata out of runtime `gTypeInfo` initialization into a static/constexpr table.

### What breaks

A straight `Type` removal would break or heavily rewrite:

- helper APIs that take `Type` (`is_integer_type`, `is_floating_point_type`, `is_struct_type`, etc.)
- `switch`-based logic such as `src/NameMangling.h:174-186` and `src/IrType.cpp`
- enum-order-dependent helpers in `src/TypeTraitEvaluator.cpp:5-13` and `src/IrGenerator_MemberAccess.cpp:2119-2129`
- parser-side range checks (`src/Parser_Core.cpp:65`, `src/Parser_Templates_Inst_Deduction.cpp:32`)
- many AST node constructors and helper signatures that currently carry `Type` directly

### What gets simpler

- One source of truth for type identity
- Fewer opportunities for `Type`/`TypeIndex` disagreement
- Template/type hashing code (`src/TemplateRegistry_Types.h`) becomes conceptually cleaner
- Alias resolution and concrete-type checks could become unified around `TypeInfo`

### What gets harder

- Bootstrap/initialization of builtin types
- Fast classification without table lookups unless the new `TypeIndex` is tagged
- Large signature churn across parser, semantic analysis, and mangling code
- Migration of range-based helpers that currently assume a stable enum order

### Performance

Today, `Type` comparisons are trivial integer compares. An extended `TypeIndex` can be equally fast **if** classification metadata is inline/tagged, or if primitive indices are reserved so that the check remains a few integer operations. A plain "always look in `gTypeInfo`" design would add indirection to hot paths and is unlikely to be a good trade for parsing, type traits, and overload resolution.

### Translating the range-based checks

The current arithmetic/fundamental helpers rely on enum ordering (`src/TypeTraitEvaluator.cpp:5-13`, `src/IrGenerator_MemberAccess.cpp:2119-2129`). Under Option B those would need to become one of:

- a primitive-kind range over tagged primitive ids, or
- a cached category bit in `TypeInfo`, or
- a switch over primitive-kind values

### Pros

- Single source of truth for semantic type identity
- Eliminates an entire class of `Type`/`TypeIndex` mismatch bugs
- Makes `TemplateRegistry_*` and alias-resolution code conceptually cleaner

### Cons

- Large refactor with broad signature churn
- Higher regression risk than the problem statement asks for
- Requires a real design for builtin bootstrapping and fast classification
- Harder to stage safely while the parser and mangler still depend heavily on raw `Type`

## 4. Option C: Hybrid approach

### Proposal

Make `TypeIndex` the **primary identity token everywhere**, but keep `Type` as a cached category field inside `TypeInfo` for fast classification.

Concretely:

- `TypeSpecifierNode` still carries a cheap category for hot-path checks
- `TypeIndex` becomes the authoritative handle for identity whenever it is valid
- `TypeInfo` retains `type_` (or a smaller `TypeCategory`) for O(1) classification
- `TypeIndex`-centric helpers are added, e.g. `is_primitive()`, `is_struct()`, `is_enum()`, `is_template_placeholder()`

### Why this is attractive here

This matches the codebase as it already exists:

- `gTypeInfo` already stores category + identity together (`src/AstNodeTypes.cpp:133-218`)
- many subsystems already read both (`src/AstNodeTypes_DeclNodes.h:837-845`, `src/TemplateRegistry_Types.h:290-320`, `src/OverloadResolution.h:1262-1303`)
- hot classification queries can still be answered without turning every branch into a deep lookup

### Best-of-both-worlds evaluation

This option gets most of Option B's coherence while avoiding a big-bang removal of `Type`:

- identity moves toward `TypeIndex`
- fast category checks remain cheap
- `NameMangling`, `IrType`, and parser code can migrate gradually
- range-based enum-order helpers can be rewritten to use cached categories instead of raw enum ordinals

The main downside is that this still retains two pieces of data, but now they serve clearer roles: **`TypeIndex` = identity, cached category = classification**.

## 5. Recommendation

### Recommended path: **Option C, staged through Option A**

Option A is the right immediate cleanup, and this branch already implements it. For the longer-term design, Option C is the best fit for FlashCpp.

Why not jump directly to Option B?

- The audit shows too many places still branch on `Type` alone (`NameMangling`, parser range checks, type traits, IR lowering).
- Builtin `TypeIndex` values are not constexpr today because `initialize_native_types()` populates them at runtime.
- A direct removal of `Type` would create a much larger regression surface than this repository needs for this problem.

Why Option C?

- It centralizes identity on `TypeIndex` without forcing a risky all-at-once rewrite.
- It makes the current `Type` field an implementation detail/cache instead of a competing source of truth.
- It gives a clean place to answer the `Template` question explicitly via `TypeIndex`/`TypeInfo` metadata instead of scattered enum comparisons.

### Rough migration plan

1. **Done now**: keep Option A helpers (`is_primitive_type`, `needs_type_index`) and remove raw duplication.
2. **Next**: add `TypeIndex`/`TypeInfo` query helpers for category checks (`isPrimitive`, `isStructLike`, `isEnum`, `isTemplatePlaceholder`).
3. **Then**: convert mixed `Type` + `TypeIndex` call sites to prefer `TypeIndex` as the source of truth when valid.
4. **Then**: replace enum-order-dependent helpers with category-based logic.
5. **Last**: audit remaining `Type`-only consumers (`NameMangling`, `IrType`, parser helpers) and decide whether `Type` stays as a cached category or can be mechanically reduced further.

### Template decision

For the immediate cleanup I recommend **not** folding `Type::Template` into `needs_type_index()` yet. The audit shows that template placeholders are real and important (`src/ExpressionSubstitutor.cpp:661`, `src/ExpressionSubstitutor.cpp:981-982`, `src/TemplateInstantiationHelper.h:305-334`), but they are still semantically different from concrete `Struct` / `Enum` / `UserDefined` types. Option C is the right place to model that distinction explicitly instead of smuggling it into one broad helper too early.
