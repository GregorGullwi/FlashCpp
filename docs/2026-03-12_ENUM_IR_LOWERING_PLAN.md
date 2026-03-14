# Enum Lowering Plan: Keep Enum Identity in Semantics, Lower Runtime Representation Earlier

**Date**: 2026-03-12  
**Status**: In Progress (Phase 0-5 complete through 2026-03-14; Phase 6 audit found remaining semantic/transitional Type uses)  
**Context**: Follow-up design note after the enum-pointer `ExprResult` / slot-4 regressions

## Proposed Approach

The proposed design direction is:

- `enum` identity is primarily a **parser / semantic / type-system** concern
- the **runtime representation** of an enum is its underlying integral type
- therefore, carrying `Type::Enum` deeply into IR/codegen for ordinary arithmetic,
  loads/stores, pointer arithmetic, and ABI lowering is more fragile than helpful

The right design is **not** to: “erase enums immediately after
parsing and pretend they are just `int` everywhere”.  Enum **identity** still
matters after parsing for:

- overload resolution
- template matching / type traits
- mangling
- `typeid` / RTTI-like behavior
- `__underlying_type`
- diagnostics and semantic checking

The proposed approach is:

> keep enum identity in the semantic type layer, but lower enum **value
> representation** to the underlying integer type at the IR/codegen boundary
> wherever the backend only needs storage/ABI/arithmetic information.

This document lays out how to achieve this incrementally.

---

## Why the Current Design Is Fragile

The current compiler mixes two different concerns into a single `Type` flow:

1. **semantic identity**  
   “this is `Color`, distinct from `int` and distinct from another enum”

2. **runtime representation**  
   “this occupies 32 bits and uses integer arithmetic / integer ABI rules”

That conflation causes codegen-specific special cases like:

- `Type::Enum` branches in `generateIdentifierIr`
- `Type::Enum` branches in `toTypedValue(...)` / `toExprResult(...)`
- pointer arithmetic needing enum-specific size recovery
- enum-pointer metadata bugs around `type_index` vs `pointer_depth`
- codegen helpers needing to know when an enum should behave like an integer and
  when its nominal identity still has to be preserved

The recent `ExprResult` regressions demonstrate this issue:

- enum pointers were semantically “enum-flavored”
- but operationally they needed pointer-depth and pointee-size behavior identical
  to other pointer-like operands
- because `Type::Enum` survived too long in the codegen contract, the slot-4
  encoding/decoding logic had to keep guessing whether a payload meant
  `type_index` or `pointer_depth`

That is a symptom of the representation boundary being too late.

---

## What Should Still Know About Enums

These layers should continue to preserve enum identity:

### 1. Parsing / AST / Type System

Enums need distinct type identity in:

- `TypeSpecifierNode`
- `EnumTypeInfo`
- enumerator declarations
- underlying-type queries
- scoped/unscoped enum rules

### 2. Semantic Analysis

Enum identity matters for:

- overload resolution ranking
- implicit conversion checks
- template argument matching
- traits (`is_enum`, `is_scalar`, `__underlying_type`, etc.)
- distinct-type diagnostics
- constant evaluation of enumerators

### 3. Mangling / Type Identity / RTTI-ish Features

When a user writes a function taking `Color`, mangling and type identity must
still see `Color`, not merely `int`.

That means:

- mangling must preserve enum type identity
- any type metadata used by `typeid`, RTTI, or reflection-like features must
  preserve enum identity
- debug / diagnostic paths should still be able to print enum names

---

## What Should Usually *Not* Need `Type::Enum`

These backend/runtime-oriented concerns should usually operate on the enum’s
lowered representation instead:

- arithmetic on enum values after semantic validation
- comparisons
- loads and stores
- assignment
- unary inc/dec
- pointer arithmetic involving enum pointees
- ABI classification for passing / returning enum values
- register allocation / machine-width selection

In other words:

> once a semantic check has decided that an operation on an enum is legal, the
> emitted runtime operation should generally use the enum’s underlying integral
> representation.

---

## Target Design: Separate `IrType` Enum

The root cause of the enum-pointer regressions (and the `UserDefined` vs `Struct`
confusion) is that the same `Type` enum is used for both semantic identity and
runtime representation. Codegen helpers can always write `if (type == Type::Enum)`
and nothing prevents it — the bug class is structurally permitted.

The fix is to make illegal states unrepresentable: introduce a **separate enum
for IR-level types** that physically cannot express `Enum`, `UserDefined`,
`Auto`, or `Template`.

### Evidence that this already works

The backend (`src/IRConverter*.h`) already operates on an implicit reduced type
set, though **this claim needs refinement**:

- `Type::Enum` appears in **~5 locations** in IRConverter files (pointer arithmetic, coercion)
- `Type::UserDefined` appears **once** (exception handling guard)
- The backend primarily discriminates on `Float` vs `Double` (SSE vs GPR), `Struct`
  (ABI classification for >64-bit values), and integer-of-size-N for everything
  else

**Note**: The claim that "Type::Enum appears zero times" is **incorrect**. 
Extensive searching found:
- 43 references to `Type::Enum` across the codebase
- 187 references to `Type::UserDefined` across the codebase
- These are concentrated in codegen helpers, not just AST/semantic layers

The intent is still valid: codegen should operate on runtime representation, 
but the migration scope is larger than originally estimated.

### The `IrType` enum

```cpp
// Runtime representation types — the only types that matter for
// register selection, ABI classification, and instruction emission.
enum class IrType : int_fast16_t {
    Void,
    Integer,        // all integer types, enums, bool — distinguished by size_in_bits + is_signed
    Float,          // 32-bit IEEE 754
    Double,         // 64-bit IEEE 754
    LongDouble,     // 80-bit x87 extended precision
    Struct,         // needs type_index for size/layout, ABI classification
    FunctionPointer,
    MemberFunctionPointer,
    MemberObjectPointer,
    Nullptr,
};
```

No `Enum`. No `UserDefined`. No `Char` vs `Int` vs `Long` (they are all
`Integer` with different `size_in_bits` and `is_signed`). No `Auto`, `Template`,
`Invalid` — those are parser/semantic concerns that must not leak into IR.

#### Why `Integer` stays coarse instead of `Int8` / `UInt8` / `Int16` / ...

For this migration, `IrType` is intentionally a **runtime family** enum, not a
full scalar-layout lattice. Width and signedness are already carried by existing
IR metadata (`size_in_bits`, `is_signed`), and most backend decisions in
FlashCpp already consume those fields. Splitting `IrType::Integer` into
`Int8`/`UInt8`/`Int16`/`UInt16`/… would multiply enum variants without removing
the need for `size_in_bits` or `is_signed`, because structs, pointers, member
pointers, and ABI-specific cases still need separate metadata anyway.

If later backend work wants a richer scalar classification, add a dedicated
scalar-layout helper or field rather than overloading `IrType` itself with every
bit-width / signedness combination.

#### Why `Struct` still relies on separate layout metadata

`IrType::Struct` only answers “this is aggregate / user-defined runtime
storage”. Exact layout remains separate on purpose:

- `type_index` answers which aggregate/member-pointer ABI rules apply
- `size_in_bits` answers current storage width
- later ABI lowering can derive calling-convention classes from that metadata

That separation is intentional. `IrType` should stay coarse; exact size/layout
should not be encoded by exploding the enum itself.

#### Why `MemberFunctionPointer`, `MemberObjectPointer`, and `Nullptr` exist

- **Member pointers** stay distinct because their representation is not a plain
  raw pointer on common ABIs. Keeping them separate prevents backend code from
  accidentally treating them as ordinary pointer integers too early.
- **`Nullptr`** exists as a transitional pointer-like runtime family so codegen
  does not silently treat `nullptr` as an integer literal during the migration.
  Once null pointer constants are lowered earlier to concrete zero/pointer-form
  values, this dedicated variant can disappear.

### Where the conversion happens

`AstToIr` is the converter. It already reads `TypeSpecifierNode` (which carries
the full `Type` enum) from AST nodes and populates `TypedValue` for IR
instructions. The conversion to `IrType` happens at exactly the points where
`TypedValue` is constructed — the same sites that currently do ad-hoc lowering.

### What it fixes

| Current bug class | Why `IrType` prevents it |
|---|---|
| Slot-4 `type_index` vs `pointer_depth` ambiguity | `IrType::Integer` with `pointer_depth > 0` is unambiguously a pointer — no heuristic needed |
| `getSizeInBytes` needing enum branches | `IrType::Integer` already carries `size_in_bits = 32` — no `gTypeInfo` lookup needed |
| Pointer stride recovery for enum pointees | `IrType::Integer` pointer with `size_in_bits = 32` → stride is 4, trivially |
| `UserDefined` vs `Struct` confusion | Both become `IrType::Struct` |
| Future contributors adding `if (type == Type::Enum)` in codegen | Won't compile — `IrType` has no `Enum` variant |

**Note**: This migration is larger than initially estimated. There are 43+ 
references to `Type::Enum` and 187+ references to `Type::UserDefined` 
across the codebase, concentrated in codegen helpers.

### `TypedValue` becomes the boundary

`TypedValue` (`src/IRTypes_Ops.h`) is the shared currency between codegen and
backend. Every IR op struct (`BinaryOp`, `AssignmentOp`, `CallOp`,
`DereferenceOp`, etc.) uses `TypedValue` for its operands. Changing
`TypedValue.type` from `Type` to `IrType` means codegen physically cannot leak
semantic-only type tags into the IR.

For the transition period, `TypedValue` can carry both fields:

```cpp
struct TypedValue {
    IrType ir_type;         // runtime representation (new, authoritative)
    int size_in_bits;
    IrValue value;
    bool is_signed;
    int pointer_depth;
    TypeIndex type_index;   // still needed for Struct layout / ABI
    // ... existing fields ...

    // Transitional: kept during migration, removed when backend is fully on ir_type
    Type semantic_type = Type::Invalid;
};
```

---

## Proposed Migration Strategy

This should be done in small phases, not as a flag day rewrite. The central
change is introducing `IrType` and migrating `TypedValue` to use it. Everything
else follows from that.

### Phase 0 — Define `IrType` and the conversion function

Add the `IrType` enum and a pure conversion function in a new header (e.g.
`src/IrType.h`):

```cpp
IrType toIrType(Type semantic_type, TypeIndex type_index = 0);
```

The mapping is:

| `Type` | `IrType` |
|--------|----------|
| `Bool`, `Char`, `UnsignedChar`, `Short`, `UnsignedShort`, `Int`, `UnsignedInt`, `Long`, `UnsignedLong`, `LongLong`, `UnsignedLongLong`, `WChar`, `Char8`, `Char16`, `Char32` | `Integer` |
| `Enum` | `Integer` (size/signedness from `EnumTypeInfo::underlying_size`) |
| `Float` | `Float` |
| `Double` | `Double` |
| `LongDouble` | `LongDouble` |
| `Struct` | `Struct` |
| `UserDefined` | `Struct` (if `getStructInfo()` exists) or `Integer` (if alias to primitive) |
| `FunctionPointer` | `FunctionPointer` |
| `MemberFunctionPointer` | `MemberFunctionPointer` |
| `MemberObjectPointer` | `MemberObjectPointer` |
| `Nullptr` | `Nullptr` |
| `Void` | `Void` |
| `Auto` | `Integer` during transition (preserves generic-lambda runtime arithmetic until `auto` is lowered earlier) |
| `Template`, `Function`, `Invalid` | assert — these must not reach IR |

This phase adds new code only. Nothing changes behavior.

### Phase 1 — Add `ir_type` field to `TypedValue`

Add small helpers with a single definition of “enum runtime representation”:

```cpp
Type getRuntimeValueType(Type semantic_type, TypeIndex type_index, PointerDepth pointer_depth);
int getRuntimeValueSizeBits(Type semantic_type, TypeIndex type_index, int semantic_size_bits, PointerDepth pointer_depth);
std::optional<ExprResult> tryMakeEnumeratorConstantExpr(...);
```

For enums, these helpers map the runtime value representation to the underlying
integral type/size from `EnumTypeInfo`, while `tryMakeEnumeratorConstantExpr`
centralizes lowering of enumerator constants to immediate IR values.

Important:

- these helpers must not erase enum identity globally
- they only answer “what should IR/runtime operations use?”

### Phase 2 — Migrate backend to read `ir_type`

**Scope Clarification**: The plan originally stated the backend has "zero Type::Enum references." 
This is incorrect - there are ~5+ references in IRConverter files. Change `IRConverter` 
handlers to read `ir_type` instead of `type`.

The scope includes:
- `IRConverter_ConvertMain.h` pointer arithmetic code (formerly `IRConverter_Conv_CorePrivate.h`)
- `IRConverter_ConvertMain.h` exception handling guard code (formerly `IRConverter_Emit_EHSeh.h`)
- Any other IRConverter files using Type::Enum/UserDefined

After this phase, the backend should no longer depend on the semantic `Type` enum for
runtime operations.

### Phase 3 — Migrate codegen helpers to read `ir_type`

This is the main work phase. Convert codegen helpers that currently branch on
`Type::Enum` or `Type::UserDefined` to use `ir_type` instead:

- `generateBuiltinIncDec` — pointer stride comes from `size_in_bits`, not
  from recovering enum pointee size via `gTypeInfo`
- direct identifier / function-argument enumerator lowering — use a shared helper
  instead of duplicating `EnumTypeInfo::findEnumerator` + underlying-type logic
- `toTypedValue(...)` / `toExprResult(...)` — slot-4 ambiguity disappears
  because `IrType::Integer` with `pointer_depth > 0` is unambiguously a pointer
- `handleLValueAssignment` / `handleLValueCompoundAssignment` — size inference
  uses `size_in_bits` directly, no `type_index` lookup needed for enums
- `getSizeInBytes` — enum/UserDefined branches become unnecessary

Helpers that legitimately need semantic identity (overload resolution, mangling,
type traits) continue to use `Type` from the AST — they never touch `TypedValue`.

**UPDATE (2026-03-13)**: A first Phase 3 slice is now in place:

- `getRuntimeValueType(...)` / `getRuntimeValueSizeBits(...)` centralize runtime
  value lowering for identifier paths and `getSizeInBytes()`
- `tryMakeEnumeratorConstantExpr(...)` centralizes enumerator-immediate lowering
  for direct identifiers, nested unscoped enum lookup, and function arguments
- `toIrType(Type::Auto)` now maps to `IrType::Integer` during the transition so
  generic-lambda arithmetic keeps integer signedness/width behavior until `auto`
  stops reaching backend arithmetic dispatch
- generic-lambda identifier lowering now falls back from unresolved local
  `Type::Auto` + `size_bits == 0` to `int`/32-bit, matching the existing
  transitional mangling/reference fallback and avoiding broken signed compares
- instantiated generic lambdas now register synthetic parameter declarations in
  their body symbol tables using the deduced `TypeSpecifierNode`, so identifier
  loads inside the body preserve narrow signed integer behavior instead of
  re-reading the original unresolved `auto` declaration

**Architecture note:** this codegen-side synthetic-declaration fix is the
smallest correct local repair for the current PR, but it is not the desired end
state. `auto` deduction and implicit-conversion normalization should still move
into a dedicated post-parse semantic pass instead of continuing to accrete in
parser/codegen boundary code.

**Layering follow-up — semantic pass is the right next step:**

The `IrType` migration solves the *type representation* boundary ("what runtime
family is this value?"), but a separate class of backend heuristics exists that
`IrType` cannot fix: **value semantics** decisions ("does this TempVar hold an
address or a scalar?").

The clearest example is the `is_likely_pointer` heuristic in
`IRConverter_ConvertMain.h` (formerly `IRConverter_Conv_VarDecl.h`, `handleVariableDecl`, reference-init-from-TempVar
path). When initializing a reference from a TempVar that isn't already tracked
in `indirect_stack_info_`, the x86 emitter must choose between:

- **MOV** — load the TempVar's value (treating it as an already-computed pointer)
- **LEA** — compute the address of the TempVar's stack slot

The backend cannot know which is correct from `IrType` alone — `IrType::Integer`
with `size_in_bits = 64` is equally ambiguous whether it holds an address or a
64-bit integer. The old code used a hand-curated type whitelist; this was replaced by
`!isIrFloatingPointType()` which was too broad (treating Void, Bool, Nullptr as pointers),
and then corrected to a positive IrType whitelist: `isIrIntegerType || isIrStructType ||
isIrPointerLikeType`. All three are heuristic guesses over a missing IR abstraction.

This is a symptom of semantic work happening at the wrong compiler level. The
correct fix is a **post-parse semantic pass** that runs between parsing and IR
emission and resolves these decisions explicitly:

1. **Reference binding** → lowered to an explicit `TakeAddress` IR op, so the
   backend just sees "store this 64-bit pointer value" with no guessing
2. **Implicit conversions** → inserted as explicit `Convert` IR ops
3. **`auto` deduction** → resolved to concrete types before IR emission
4. **`nullptr` contextual conversions** → lowered to typed zero constants
5. **Enum identity preservation** → semantic pass retains identity for overload
   resolution / mangling, then strips it for IR emission (replacing the current
   ad-hoc `carriesSemanticTypeIndex` / `getRuntimeValueType` helpers)

This would also eliminate the former Phase 4 blockers (`setReferenceInfo`,
`IndirectStorageInfo`, `TypeSpecifierNode` constructors in IRConverter) — all of
which exist because the backend is making semantic decisions that should have
been resolved earlier. (**UPDATE 2026-03-14**: These blockers have been resolved
by adding parallel `ir_type` fields and centralising the TypeSpecifierNode
construction into a single lambda.)

After the `IrType` migration is complete, the separation should be made explicit
between:

1. **runtime family** (`IrType`: integer / float / struct / pointer-family)
2. **scalar/layout metadata** (`size_in_bits`, `is_signed`, `type_index`)
3. **semantic normalization** (reference binding, implicit conversions, enum
   identity, generic-lambda `auto`, `nullptr` contextual conversions) in a
   dedicated semantic pass

That keeps `IrType` small while moving policy decisions out of parser/codegen
glue and into the correct compiler layer. The `IrType` migration is necessary
groundwork — it cleans up the type boundary — but the semantic pass is what
ultimately eliminates the class of bugs where the backend infers intent from
type metadata.

**UPDATE (2026-03-14)**: A second Phase 3 slice is now in place:

- `carriesSemanticTypeIndex(Type)` helper added to `IrType.h` — centralizes the
  recurring `Type::Struct || Type::Enum || Type::UserDefined` pattern into a
  single named function.  Used by identifier lowering, ExprResult construction,
  and inc/dec metadata paths.
- `getSizeInBytes` now uses `isIrStructType(toIrType(type))` — catches both
  `Type::Struct` and `Type::UserDefined` (typedef-to-struct aliases) in the
  struct-layout path, while preserving the assert for genuine `Type::Struct`.
- `handleLValueAssignment` / `handleLValueCompoundAssignment` `inferLValueSizeBits`
  now uses `isIrStructType(toIrType(lvalue_type))` instead of the two-way
  `Type::Struct || Type::UserDefined` disjunction.
- `populateIncDecTypedValueMetadata` in `generateBuiltinIncDec` now uses
  `carriesSemanticTypeIndex(typed_value.type)` — previously missed
  `Type::UserDefined` (only checked Struct and Enum).
- Return type_index patterns in `IrGenerator_Call_Direct.cpp` and
  `IrGenerator_Call_Indirect.cpp` now use `isIrStructType(toIrType(...))`.
- Struct checks in `IRConverter_ConvertMain.h` (formerly `IRConverter_Conv_Calls.h`, constructor overload resolution
  and implicit copy/move detection) migrated to `isIrStructType(toIrType(...))`.
- `carries_type_index` patterns in `IrGenerator_Expr_Primitives.cpp` now use
  `carriesSemanticTypeIndex(...)` and `isIrStructType(toIrType(...))`.
- Struct member type checks in `IrGenerator_MemberAccess.cpp` (trivially-copyable,
  trivial, struct info lookup, this-pointer resolution) migrated.
- Nested struct detection in `IrGenerator_Stmt_Decl.cpp` and
  `IrGenerator_Visitors_TypeInit.cpp` (recursive zero-fill) migrated.

**Remaining Phase 3 work:**
- `IrGenerator_Expr_Operators.cpp` lines 1037, 1046, 1138, 1145 — operator overload
  applicability (semantic: checks Type::Enum for overload semantics)
- `IrGenerator_NewDeleteCast.cpp` lines 730, 733, 767, 774-777 — semantic identity
  and enum↔int cast rules
- `IrGenerator_MemberAccess.cpp` line 1962 (`isScalarType`) — includes Type::Enum in
  scalar classification (semantic)
- `IrGenerator_MemberAccess.cpp` line 2962 — `__underlying_type` trait (semantic)
- `IrGenerator_MemberAccess.cpp` line 3518 — fallback suppression for enums (semantic)

**UPDATE (2026-03-14)**: A third Phase 3 slice is now in place:

- `requiresUserDefinedOp` lambdas in `IrGenerator_Expr_Operators.cpp` (lines 793, 798)
  now use `isIrStructType(toIrType(base_type))` — catches both Struct and UserDefined
  aliases without explicit two-way OR.
- `param_type.type() != Type::Struct && param_type.type() != Type::UserDefined`
  check (line 899) simplified to `!isIrStructType(toIrType(param_type.type()))`.
- Constructor overload identity check in `IrGenerator_Call_Direct.cpp` (line 845)
  migrated to `isIrStructType(toIrType(arg_type))`.
- `base_type != Type::Struct && base_type != Type::UserDefined` checks in
  `IrGenerator_MemberAccess.cpp` (lines 1009, 3383, 3408) all migrated to
  `!isIrStructType(toIrType(...))`.
- `isTwoRegisterStructRaw` in `IRConverter_ConvertMain.h` (formerly `IRConverter_Emit_CompareBranch.h`) now takes
  `IrType ir_type` instead of `Type type`; all callers updated to use
  `toIrType(...)` or `arg.effectiveIrType()`.
- Remaining TypedValue.type **write** sites: all now have companion
  `ir_type = toIrType(...)` assignments alongside the `type =` write, across
  `IrGenerator_Call_Direct.cpp`, `IrGenerator_Call_Indirect.cpp`,
  `IrGenerator_Expr_Conversions.cpp`, `IrGenerator_Expr_Operators.cpp`,
  `IrGenerator_Expr_Primitives.cpp`, `IrGenerator_Lambdas.cpp`,
  `IrGenerator_MemberAccess.cpp`, `IrGenerator_Stmt_Decl.cpp`,
  `IrGenerator_Visitors_Decl.cpp` (~65 additional sites).
- `ExprResult.type == Type::Void` sentinel checks in `IrGenerator_Expr_Primitives.cpp`
  and `IrGenerator_Expr_Operators.cpp` migrated to `effectiveIrType() == IrType::Void`.
- `IRConverter_ConvertMain.h` struct identity check (formerly in `IRConverter_Conv_Calls.h`) migrated to
  `!isIrStructType(arg.effectiveIrType())`.

**Remaining semantically-intentional Phase 3 sites (intentionally kept as Type::X):**
- `IrGenerator_Expr_Operators.cpp` lines 1037, 1046, 1138, 1145 — operator overload
  applicability (semantic: checks Type::Enum for overload semantics)
- `IrGenerator_NewDeleteCast.cpp` lines 730, 733, 767, 774-777 — semantic identity
  and enum↔int cast rules
- `IrGenerator_MemberAccess.cpp` line 1962 (`isScalarType`) — includes Type::Enum in
  scalar classification (semantic)
- `IrGenerator_MemberAccess.cpp` line 2962 — `__underlying_type` trait (semantic)
- `IrGenerator_MemberAccess.cpp` line 3518 — fallback suppression for enums (semantic)

These remaining sites are **semantic checks** that intentionally need `Type::Enum`
or the full `Type` enum.  They will stay as-is until a semantic analysis pass
provides a clean way to query semantic identity without touching the runtime type field.

### Phase 4 — Remove `Type type` from `TypedValue`

**Phase 4 preparation is now complete (2026-03-14):**
- All TypedValue **write** sites (`.type = ...`) now have companion `ir_type = toIrType(...)` writes.
- All TypedValue **read** sites (`.type == Type::X` comparisons) in IRConverter and CodeGen
  have been migrated to `effectiveIrType()` or `isIrStructType(toIrType(...))`.
- Zero TypedValue.type equality-comparison reads remain in codegen/IRConverter code.

**Phase 4 blocker resolution (2026-03-14):**
All previously listed blockers have been resolved:

- ~~`IndirectStorageInfo` / `setReferenceInfo` migration~~ — **DONE**: `IndirectStorageInfo`
  and `TempVarMetadata` now have parallel `IrType ir_type` fields, auto-populated via
  `toIrType()` in `setIndirectStorageInfo`, `getReferenceInfo`, and all `TempVarMetadata`
  factory methods (`makeReference`, `makeAddressOnly`, `makeLValue`, `makeXValue`).
- ~~`TypeSpecifierNode(arg.type, ...)` in `IRConverter_Conv_Calls.h`~~ — **DONE**: Extracted
  `buildTypeSpecFromTypedValue` lambda that uses `effectiveIrType()` for struct detection.
  The `.type` dependency is centralised into a single location for later replacement
  (TODO: Phase 5 IrType-based `TypeSpecifierNode` construction).
- ~~`IRConverter_Conv_Arithmetic.h` line 1161 unused debug variable~~ — **DONE**: Removed from code now living in `IRConverter_ConvertMain.h`.
- ~~`IRConverter_Conv_VarDecl.h` lines 213, 244 debug logging of `init.type`~~ — **DONE**:
  Migrated to `init.effectiveIrType()`.
- ~~`is_likely_pointer` heuristic regression~~ — **DONE**: Replaced overly broad
  `!isIrFloatingPointType(...)` with positive whitelist:
  `isIrIntegerType || isIrStructType || isIrPointerLikeType`.
- ~~Unmigrated `TypedValue` aggregate inits~~ — **DONE**: `IrGenerator_Expr_Conversions.cpp`
  `storeBackUpdatedValue` migrated to `makeTypedValue()`. New
  `makeTypedValue(Type, SizeInBits, IrValue, ReferenceQualifier)` overload added. All 6
  designated-init sites in `IrGenerator_Call_Indirect.cpp` migrated.
- ~~`carriesSemanticTypeIndex` widening for primitive typedefs~~ — **DONE**: Added
  `type_index.is_valid()` guard at `IrGenerator_Expr_Primitives.cpp:1034` so primitive
  typedefs (`typedef int MyInt` → `Type::UserDefined`) don't propagate stale type_index.
- ~~`is_signed` not propagated through `makeTypedValue`~~ — **DONE**: Set
  `ctx.result_value.is_signed = isSignedType(result_type)` after `makeTypedValue`
  construction in `setupAndLoadArithmeticOperation`.

**Remaining before full removal:**
Once all consumers read `ir_type`, remove the old `type` field. Any remaining
code that tries to match on `Type::Enum` inside a codegen helper will fail to
compile — the bug class becomes structurally impossible.

### Phase 5 — Simplify `ExprResult`

**UPDATE (2026-03-13)**: The ExprResult migration is **already complete**. The old
slot-4 positional encoding, `encoded_metadata`, `preserveEncodedExprMetadata`, and
`toExprResult(...)` shim have all been removed. `ExprResult` now uses named fields
(`type`, `size_in_bits`, `value`, `type_index`, `pointer_depth`). The original
slot-4 ambiguity (same slot for `type_index` and `pointer_depth`) no longer exists.

**UPDATE (2026-03-14)**: `ExprResult` now has `IrType ir_type = IrType::Void` field
and `effectiveIrType()` method (mirroring TypedValue). `makeExprResult(...)` always
populates `ir_type = toIrType(type)`. All `ExprResult.type == Type::Void` sentinel
checks have been migrated to `effectiveIrType() == IrType::Void`.

Remaining work for this phase:
- `makeExprResult(...)` helpers could eventually accept `IrType` instead of `Type`
- `toTypedValue(const ExprResult&)` already copies `ir_type` directly

### Phase 6 — Audit and cleanup

- verify `Type::Enum` and `Type::UserDefined` appear only in:
  - `Parser.cpp` / AST construction
  - `OverloadResolution.h` (semantic ranking)
  - `NameMangling.h` (symbol identity)
  - `TypeTraitEvaluator.h` (`is_enum`, `__underlying_type`)
  - `TemplateRegistry.h` (template argument matching)
  - `ConstExprEvaluator.h` (constant evaluation)
  - diagnostics / error messages
- remove any remaining `Type::Enum` / `Type::UserDefined` branches in
  `CodeGen_*.cpp`, `IROperandHelpers.h`, `IRConverter*.h`

**Audit result (2026-03-14): not clean yet — keep this document**

- `IROperandHelpers.h` is clean: no remaining `Type::Enum` / `Type::UserDefined` references.
- `IRConverter*.h` runtime dispatch is clean, but there is still one transitional runtime/diagnostic
  check in `IRConverter_ConvertMain.h` (formerly `IRConverter_Conv_Memory.h`, `op.result.type != Type::Struct`) used to distinguish
  unresolved `Type::UserDefined` metadata from a genuine zero-sized-struct bug. The other
  `IRConverter*.h` matches are comments describing the migration.
- Remaining `CodeGen_*.cpp` references are concentrated in semantic or template-driven logic:
  - `IrGenerator_NewDeleteCast.cpp` — semantic identity checks and enum↔int cast rules.
  - `IrGenerator_Expr_Operators.cpp` — overload applicability / user-defined-operator detection.
  - `IrGenerator_MemberAccess.cpp` — semantic scalar classification, `__underlying_type`,
    conversion-operator alias resolution, and enum fallback suppression.
  - `IrGenerator_Call_Direct.cpp` — unresolved dependent template return types still represented as
    `Type::UserDefined`.
  - `IrGenerator_Helpers.cpp` / `IrGenerator_Expr_Primitives.cpp` — enum underlying-type recovery and
    enumerator/type-index preservation helpers that still start from semantic `Type`.
  - `IrGenerator_Visitors_Namespace.cpp` / `IrGenerator_Visitors_TypeInit.cpp` — AST/type-node
    construction paths, not IR runtime dispatch.

So the Phase 6 cleanup criterion:

> `Type::Enum` and `Type::UserDefined` do not appear in `CodeGen_*.cpp`, `IROperandHelpers.h`,
> or `IRConverter*.h`

is **not yet satisfied**. The plan document should remain until those semantic and transitional
sites are either migrated to a dedicated semantic-analysis layer or explicitly carved out as
intentional long-term exceptions.

---

## Non-Goals

This plan is **not** proposing:

- deleting `EnumTypeInfo`
- treating enums as interchangeable with integers in overload resolution
- mangling enum parameters as integers
- removing enum identity from diagnostics or traits
- a broad one-shot rewrite across the whole compiler

The goal is to make the **representation boundary** structurally enforced by the
type system rather than relying on discipline.

---

## Risks

### 1. Over-eager lowering can break overload resolution semantics

If enum identity is erased too early, calls like:

```cpp
void f(int);
void f(Color);
```

will be misresolved.

So lowering must happen **after** semantic selection, not before.

### 2. Mangling and RTTI must keep nominal enum identity

Any path that affects symbol identity or runtime type identity must still
preserve enum-ness.

### 3. Pointer metadata needs a real representation model

Lowering enum values alone will not fix pointer issues unless pointee size is
also made explicit.  The recent `++(*pp)` regression shows this clearly.

### 4. UserDefined / typedef / alias paths have similar shape

This enum work will likely expose the same semantic-vs-representation split for
other nominal types.

### 5. **NEW** Type::UserDefined serves dual purposes

`Type::UserDefined` is used for:
- Actual user-defined types (classes, structs) → should become `IrType::Struct`
- Template parameter placeholders (dependent types) → must be resolved **before** codegen
- Type aliases

The migration must distinguish between "real" user-defined types and template
parameters. Template parameters should NOT reach codegen - they must be resolved
earlier in template instantiation.

### 6. ~~ExprResult slot-4 encoding is structurally flawed~~ (RESOLVED)

**UPDATE (2026-03-13)**: This risk is **resolved**. The ExprResult migration
(completed 2026-03-12, see `docs/EXPR_RESULT_MIGRATION.md`) removed the slot-4
encoding entirely. `ExprResult` now uses named fields with strong wrapper types
(`TypeIndex`, `PointerDepth`, `SizeInBits`), making the ambiguity structurally
impossible.

---

## Impact Analysis

### Files Likely to Require Modifications

The following files contain code that directly handles `Type::Enum` or `Type::UserDefined` 
and will need review during implementation:

| File | Type of Changes Expected |
|------|-------------------------|
| `IrGenerator_Expr_Primitives.cpp` | Enum-to-runtime lowering in primitive operations (~15+ refs) |
| `IrGenerator_Expr_Conversions.cpp` | Conversion logic between enum and integral types |
| `IrGenerator_Expr_Operators.cpp` | Binary/unary operator handling for enums (~10+ refs) |
| `IrGenerator_Call_Direct.cpp` | Argument passing and return value handling |
| `IrGenerator_Helpers.cpp` | Enum size/type resolution helpers |
| `IrGenerator_NewDeleteCast.cpp` | Cast handling between enum and int |
| `IrGenerator_MemberAccess.cpp` | Member access for enum types |
| `IROperandHelpers.h` | **CRITICAL** - Slot-4 encoding redesign (~6 refs) |
| `IRConverter_ConvertMain.h` | IR conversion logic (pointer arithmetic; includes code formerly in `IRConverter_Conv_CorePrivate.h`) |
| `OverloadResolution.h` | Ranking and resolution logic (semantic, unchanged) |
| `NameMangling.h` | Type identity preservation (semantic, unchanged) |
| `Parser_Decl_StructEnum.cpp` | Enum parsing (semantic, unchanged) |
| `Parser_Templates_*.cpp` | Template instantiation (~50+ refs to Type::UserDefined!) |

**Scope Note**: The plan originally estimated ~8 files. Actual scope is 25+ files 
due to extensive usage of Type::Enum and Type::UserDefined in codegen.

### Components With Indirect Impact

These components use enum information but may not need direct changes:

- **Template instantiation** - Uses enum identity for matching; should be unaffected
- **Type traits** - `is_enum`, `__underlying_type` work at semantic layer; unchanged
- **Debug info** - Enum names come from semantic type; should be unaffected

### Expected Benefits

- Reduced fragility in codegen from explicit representation boundary
- Cleaner pointer arithmetic handling via explicit pointee-size helpers
- Simpler `ExprResult` encoding by reducing special-case enum handling
- Easier future maintenance when adding new nominal types

### Migration Complexity

The phased approach allows incremental migration:

- **Phase 0-1** (`IrType` definition + `TypedValue` dual field): Low risk, additive only — ✅ **COMPLETE**
- **Phase 2** (backend migration): Medium risk - now known to have ~5+ references to Type::Enum — ✅ **COMPLETE**
- **Phase 3** (codegen helper migration): **High risk** - main body of changes (~43+ Type::Enum, 187+ Type::UserDefined) — ✅ **COMPLETE**
- **Phase 4** (remove old `Type` field blockers): Low risk — ✅ **COMPLETE** (all blockers resolved; `type` field removal is mechanical)
- **Phase 5** (ExprResult simplification): ✅ **COMPLETE** (slot-4 encoding already resolved by ExprResult migration)
- **Phase 6** (audit and cleanup): Remaining work — verify `Type::Enum`/`Type::UserDefined` only in semantic layers

**Revised Estimate**: Work is approximately **3-4x larger** than originally estimated 
due to extensive codegen usage of Type::Enum and Type::UserDefined.

---

## Proposed First Slice

The narrowest, safest starting point:

1. add `src/IrType.h` with the `IrType` enum and `toIrType(Type, TypeIndex)`
2. add `IrType ir_type` field to `TypedValue` and `ExprResult`, populate it at all
   construction sites (mechanical, no behavior change)
3. update `toTypedValue(const ExprResult&)` and `makeExprResult(...)` to carry `ir_type`
4. add a static assert or `[[deprecated]]` on `TypedValue::type` to catch any
   new code that sets it directly — this surfaces sites that still need updating
5. add focused tests that would fail if `Type::Enum` leaked into IR arithmetic:
   - enum pointer increment/decrement stride
   - enum pointer array subscript
   - overload resolution still preferring enum overloads over integer overloads
   - mangling for enum parameters unchanged

**Note**: The original item 3 ("redesign IROperandHelpers.h slot-4 encoding") is
**no longer needed** — the ExprResult migration already removed the slot-4 encoding
and replaced it with named fields.

---

## Success Criteria

The enum lowering refactoring is considered complete when:

### Code Quality Metrics

- `TypedValue.type` field removed — `IrType` is the only type field in IR op structs
- `Type::Enum` and `Type::UserDefined` do not appear in `CodeGen_*.cpp`, `IROperandHelpers.h`, or `IRConverter*.h`
- `toExprResult(...)` slot-4 heuristics removed — pointer vs type_index ambiguity no longer exists
- Pointer stride comes from `size_in_bits` directly, no `gTypeInfo` lookup in codegen arithmetic

### Functional Requirements

- All existing enum tests pass (arithmetic, comparisons, pointers, arrays)
- Overload resolution correctly prefers enum overloads over integer overloads
- Mangling for enum parameters remains unchanged
- `typeid` and RTTI-like features preserve enum identity
- `__underlying_type` and type traits work correctly

### Regression Prevention

- The original enum-pointer `ExprResult` regression (slot-4 encoding) does not recur
- Pointer arithmetic on enum pointers works correctly
- Enum values passed by value or reference follow correct ABI rules

### Cleanup Verification

- `Type::Enum` and `Type::UserDefined` remain only in:
  - `Parser*.cpp` / AST construction
  - `OverloadResolution.h` (semantic ranking)
  - `NameMangling.h` (symbol identity)
  - `TypeTraitEvaluator.h` (`is_enum`, `__underlying_type`)
  - `TemplateRegistry*.cpp` / `Parser_Templates*.cpp` (template parameter matching)
  - `ConstExprEvaluator*.cpp` (constant evaluation)
  - diagnostics / error messages
- `TypedValue`, `ExprResult`, and all IR op structs use `IrType` only

**Note**: This is a larger scope than originally estimated. The original plan 
assumed ~8 files would need changes; actual scope is 25+ files.

### Test Coverage

| Test File | Coverage |
|-----------|----------|
| `test_enum_ret0.cpp` | Basic enum, enum class, explicit values, underlying type |
| `test_enum_class_mangling_ret0.cpp` | Mangling for enum class |
| `test_enum_implicit_conv_ret0.cpp` | Implicit conversion to int/unsigned/long |
| `test_enum_pointer_increment_ret30.cpp` | Pointer increment on enum pointers |
| `test_enum_pointer_array_subscript_ret0.cpp` | Array subscript on enum pointer arrays |
| `test_enum_scoped_selfref_ret0.cpp` | Scoped enum with self-referencing enumerators |
| `test_c_style_casts_ret65.cpp` | C-style casts with enums |
| `test_type_traits_intrinsics_ret147.cpp` | `__is_enum` trait |
| `test_underlying_type_ret42.cpp` | `__underlying_type` trait |
| `test_using_enum_ret6.cpp` | C++20 using enum |
| `test_switch_10_ret10.cpp` | Switch statements with enums |
| `test_enum_comparison_ret0.cpp` | Enum comparisons |
| `test_enum_bitwise_ret0.cpp` | Enum bitwise operations |
| `test_enum_increment_decrement_ret0.cpp` | Enum increment/decrement |
| `test_enum_compound_assign_ret0.cpp` | Enum compound assignment (`+=`, `|=`, `-=`) |
| `test_enum_lvalue_assign_ret0.cpp` | Enum lvalue assignment (array element, pointer deref, stride) |
| `test_struct_typedef_alias_ret0.cpp` | Struct typedef alias (UserDefined → IrType::Struct path) |
| `test_void_ptr_ref_init.cpp` | `void*` reference init — `is_likely_pointer` heuristic (Phase 4) |

### Known Gaps

**Note**: Some items previously listed as gaps actually have test coverage:

- ~~Direct ++/~~ on enum values~~ - EXISTS (`test_enum_increment_decrement_ret0.cpp`)
- ~~Enum bitwise compound assignment~~ - EXISTS (`test_enum_bitwise_ret0.cpp`)
- ~~enum class~~ - EXISTS (`test_enum_class_mangling_ret0.cpp`, `test_c_style_casts_ret65.cpp`)

**Actual remaining gaps to verify during migration:**
- Enum to pointer casts (C-style)
- Enum underlying type explicit specification with non-int types
- Enum forward declarations
- Enum in template template parameters

### Verification Tests

The following tests should compile and return 0 when the enum lowering work is complete:

#### Enum Arithmetic Test

```cpp
enum Color : int { Red = 1, Green = 2, Blue = 3 };

int test_enum_plus_enum() {
    int c = Red + Green;  // Should be 3
    return c == 3 ? 0 : 1;
}

int test_enum_minus_enum() {
    int c = Blue - Red;  // Should be 2
    return c == 2 ? 0 : 2;
}
```

#### Enum Overload Resolution Test

```cpp
enum Color { Red = 1, Green = 2, Blue = 3 };

int overloaded(int x) { return 1; }
int overloaded(Color c) { return 2; }

int test_enum_preferred_over_int() {
    Color c = Green;
    // Should call overloaded(Color), not overloaded(int)
    return overloaded(c) == 2 ? 0 : 1;
}
```

#### Enum Class Test

```cpp
enum class Status { Pending = 0, Running = 1, Done = 2 };

int test_enum_class() {
    Status s = Status::Running;
    return s == Status::Running ? 0 : 1;
}
```

---

## Summary

This plan proposes:

- Introduce `IrType` — a reduced enum that cannot express `Enum`, `UserDefined`,
  `Auto`, or `Template` — as the type field in `TypedValue` and all IR op structs
- `AstToIr` converts `Type` → `IrType` at the point where IR instructions are
  built, keeping enum identity in the AST/semantic layer where it belongs
- The backend (`IRConverter`) never sees `Type::Enum` or `Type::UserDefined` —
  this is already true in practice; `IrType` makes it a compile-time guarantee
- The slot-4 encoding ambiguity has already been resolved by the ExprResult migration.
  The remaining enum-pointer stride bugs, and `getSizeInBytes` special cases all
  disappear as natural consequences of the type boundary
