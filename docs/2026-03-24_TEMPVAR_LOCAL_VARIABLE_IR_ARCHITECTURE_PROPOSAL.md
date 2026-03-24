# TempVar / local-variable IR architecture proposal (2026-03-24, revised post-#1007)

## Scope

This note audits the current TempVar/local-variable IR architecture around reference binding and
address-producing temporaries.

Files inspected for this proposal:

- `/src/IRTypes_Registers.h` (`TempVarMetadata`, `ValueCategory`, `LValueInfo`)
- `/src/IRTypes_Ops.h` (`TypedValue` and all relevant op structs)
- `/src/IRConverter_ConvertMain.cpp` (especially `handleVariableDecl`, `setIndirectStorageInfo`,
  `handleArrayAccess`, `handleArrayElementAddress`, `handleMemberLoad`, `handleAddressOf`,
  `handleAddressOfMember`, `handleComputeAddress`, and call handling)
- `/src/IrGenerator_Stmt_Decl.cpp`
- `/src/IrGenerator_Expr_Primitives.cpp`
- `/src/IrGenerator_Expr_Conversions.cpp`
- `/src/IrGenerator_Call_Direct.cpp`
- `/src/IrGenerator_Call_Indirect.cpp`
- `/src/IrGenerator_MemberAccess.cpp`
- `/src/IROperandHelpers.h`
- `/src/IrGenerator_Expr_Operators.cpp`

---

## Status snapshot (as of main after #1007)

The immediate bugs described in the original issue (wrong value / SIGSEGV for ref-to-ref and
const-ref-to-prvalue) were fixed in #1007.  That fix:

1. Added `setTempVarMetadata(addr_temp, TempVarMetadata::makeAddressOnly(...))` immediately after
   emitting `ArrayElementAddressOp` for regular array-subscript reference init
   (`src/IrGenerator_Stmt_Decl.cpp:1735-1738`).
2. Added the same call after emitting `ArrayElementAddressOp` for structured-binding array refs
   (`src/IrGenerator_Stmt_Decl.cpp:2674-2677`).
3. Added the same call after emitting `ComputeAddressOp` for structured-binding member refs
   (`src/IrGenerator_Stmt_Decl.cpp:3096-3099`).
4. Tightened `handleVariableDecl` to check `getTempVarLValueInfo` and `getTempVarMetadata` before
   falling back to `is_likely_pointer`, and removed `isIrIntegerType` from the heuristic so that
   `long long` function return values are no longer misclassified as addresses
   (`src/IRConverter_ConvertMain.cpp:6236-6261`).

**This is progress, but it is still the manual-synchronisation approach.**  Adding a new
address-producing op still requires a developer to remember two steps: call
`setTempVarMetadata(...)` at the IR-generation site *and* keep the heuristic condition in sync
with any novel IR types that might be produced.  The architectural root cause is unchanged.

---

## Implementation progress (as of 2026-03-24)

All phases are complete.  All 1 734 tests pass.

### Phase 1 — complete

`ValueStorage` enum added to `src/IRTypes_Ops.h`.  `TypedValue::storage` and
`ExprResult::storage` fields added.  `toTypedValue(ExprResult)` propagates the field.
`handleVariableDecl` in `src/IRConverter_ConvertMain.cpp` checks the field:
`ContainsAddress` → MOV, `ContainsData` → LEA.

### Phase 2 — complete (all known address-producing sites annotated)

All IR-generator sites that produce a TempVar holding a 64-bit address for reference binding
are now marked `ContainsAddress`:

| File | Sites |
|---|---|
| `IrGenerator_Stmt_Decl.cpp` | `ArrayElementAddressOp` (regular ref + structured binding) + `ComputeAddressOp` (structured binding) |
| `IrGenerator_Expr_Conversions.cpp` | `ComputeAddressOp` (unary `&`), `ArrayElementAddressOp` (unary `&arr[i]`), `BinaryOp Add` (pointer+member offset), `AddressOfMember`, dereference-to-lvalue-reference copy |
| `IrGenerator_Expr_Primitives.cpp` | `AssignmentOp(dereference=false)` reference-parameter and reference-variable LValueAddress paths |
| `IrGenerator_NewDeleteCast.cpp` | `handleRValueReferenceCast` and `handleLValueReferenceCast` |
| `IrGenerator_Call_Direct.cpp` | reference (`T&`/`T&&`) call returns |
| `IrGenerator_Call_Indirect.cpp` | reference returns (virtual and non-virtual paths share the same annotation; VirtualCallOp gap from §1C is now closed) |
| `IrGenerator_MemberAccess.cpp` | reference member LValueAddress path + struct reference member load path |

### Phase 3 — complete (all ExprResult factory call sites annotated, refactored to compile-time enforcement)

`ValueStorage storage` is now a **required 6th parameter** of `makeExprResult()`.  The
`ExprResult withStorage(ExprResult, ValueStorage)` wrapper has been deleted.  Omitting the
`storage` argument is now a **compile error**, not a silent `LegacyUnclassified` default.

`makeMemberResult()` also received the same treatment: `storage` added as a required 6th
parameter (default params on `TypeIndex`/`PointerDepth` removed too), and four previously-
unannotated call sites were annotated.

`withStorage(TypedValue, ValueStorage)` is kept for the two `makeTypedValue` sites in
`IrGenerator_Stmt_Decl.cpp` that set `ContainsAddress` on structured-binding initializers.

Summary of annotated sites per file:

| File | `ContainsData` sites | `ContainsAddress` sites |
|---|---|---|
| `IrGenerator_Expr_Operators.cpp` | 45 | 0 |
| `IrGenerator_Expr_Conversions.cpp` | 40 | 0 |
| `IrGenerator_MemberAccess.cpp` | 27 | 2 |
| `IrGenerator_Expr_Primitives.cpp` | 14 | 2 |
| `IrGenerator_NewDeleteCast.cpp` | 12 | 1 |
| `IrGenerator_Call_Indirect.cpp` | 7 | 0 |
| `IrGenerator_Call_Direct.cpp` | 8 | 0 |
| `IrGenerator_Visitors_Decl.cpp` | 3 | 0 |
| `IrGenerator_Lambdas.cpp` | 2 | 0 |
| `IrGenerator_Helpers.cpp` | 1 | 0 |
| `AstToIr.h` | 1 | 0 |

### Phase 4 — complete (heuristic deleted, `LegacyUnclassified` removed)

The `LegacyUnclassified` enum value was removed from `ValueStorage`.  `TypedValue::storage`
and `ExprResult::storage` default to `ContainsData`.  `handleVariableDecl` uses a simple
`const bool is_likely_pointer = (init.storage == ValueStorage::ContainsAddress)` — no
fallback branch, no TempVar metadata lookup, no heuristic.

The `tempAlreadyHoldsAddress` lambda in `IrGenerator_Expr_Operators.cpp` was simplified to
`return argument_result.storage == ValueStorage::ContainsAddress`.

The `makeArrayResult` factory in `generateArraySubscriptIr` was updated to accept `ValueStorage`
as a required parameter (matching `makeExprResult`/`makeMemberResult`).

### Phase 5 — complete (dead-code removal; Phase 5 in old plan folded into Phase 4 cleanup)

All migration-era dead code removed: the `lvalue_info_opt`/`has_indirect_lvalue`/`temp_meta`/
`holds_address` local variables in `handleVariableDecl`, the InternalError throw for
`LegacyUnclassified`, migration comments in `IROperandHelpers.h`.

Note: `TempVarMetadata.is_address`, `holds_address_only`, `setAddressOnlyInfo`, and
`setReferenceInfo` in `IRConverter_ConvertMain.cpp` are still present — they drive the
`indirect_stack_info_` system used for function parameters and 'this' registration, which is
a separate concern from `ExprResult.storage` and is not yet redundant.

### Remaining work

- **Option B** (optional readability): add `CopyReferenceOp` opcode (see §9).

---

## Executive summary

The current design has **two separate sources of truth** for "does this TempVar stack slot already
hold an address?":

1. `TempVarMetadata` / `GlobalTempVarMetadataStorage` in the IR generator
   (`src/IRTypes_Registers.h:243-353`, `src/IRTypes_Ops.h:246-339`)
2. `indirect_stack_info_` in the converter
   (`src/IRConverter_ConvertMain.cpp:1752-1797`, `1800-1897`)

`handleVariableDecl` bridges the gap with a heuristic when neither channel has the fact
(`src/IRConverter_ConvertMain.cpp:6235-6271`):

```cpp
// Narrowed after #1007 — isIrIntegerType removed, but two arms remain:
bool is_likely_pointer = has_indirect_lvalue || holds_address ||
    (init.size_in_bits == SizeInBits{64} &&
     (isIrStructType(init_ir) || isIrPointerLikeType(init_ir)));
```

The problem is not just one bad condition.  The problem is that **the IR carries no single,
authoritative, constructor-set fact** for:

- "this TempVar stack slot contains a data value"
- "this TempVar stack slot contains the address of an existing object"

As long as that fact is reconstructed post-hoc, any new address-producing op risks introducing a
bug.

---

## Clarification: this is a codegen-layer problem, not a sema gap

`REVIEW.md` asks whether code generation is doing lookups or fallbacks because semantic analysis
failed to model something earlier.

For this issue, `IRConverter` reading `TempVarMetadata` / `LValueInfo` is an established
**codegen-to-codegen metadata contract** (`src/IRTypes_Ops.h:246-267`; see for example
`src/IRConverter_ConvertMain.cpp:8198-8200`, `src/IrGenerator_Expr_Operators.cpp:4183-4253`,
`src/IrGenerator_MemberAccess.cpp:1300-1325`).  That pattern is correct and intentional.

The architectural problem is narrower: the *same* "this temp holds an address" fact is represented
in multiple codegen-layer channels, some paths seed only one channel, and `handleVariableDecl`
guesses when neither is seeded.

---

## 1. Ops whose TempVar slot can hold an address rather than a data value

### 1A. Correctly tracked — metadata set in converter

#### `AddressOfOp`
- Definition: `src/IRTypes_Ops.h:591-594`
- Converter calls `setAddressOnlyInfo(...)` (`src/IRConverter_ConvertMain.cpp:13130-13208`)
- Status: **converter-side only** — no generator-side metadata
- Semantics: plain C++ pointer, not a C++ reference

#### `AddressOfMemberOp`
- Definition: `src/IRTypes_Ops.h:597-603`
- Converter calls `setAddressOnlyInfo(...)` (`src/IRConverter_ConvertMain.cpp:13271-13315`)
- Status: **converter-side only**

### 1B. Correctly tracked — metadata set in generator

#### `CallOp` / `IndirectCallOp` when callee returns `T&` or `T&&`
- Direct-call path: `setTempVarMetadata(...makeXValue / makeLValue...)` on ret_var
  (`src/IrGenerator_Call_Direct.cpp:1588-1598`)
- Member / indirect call path: same (`src/IrGenerator_Call_Indirect.cpp:1656-1666`)
- Status: **generator metadata set**; for `T&&` the converter *also* mirrors via
  `setIndirectStorageInfo(...)` (`src/IRConverter_ConvertMain.cpp:4491-4498`) — dual tracking
  already present for rvalue references

#### `MemberLoadOp` for reference members
- Generator sets `makeLValue / makeXValue` on the result
  (`src/IrGenerator_MemberAccess.cpp:1329-1346`, `1392-1421`)
- Converter calls `setReferenceInfo(...)` (`src/IRConverter_ConvertMain.cpp:12632-12636`)
- Status: **set in both places** (dual tracking)

#### `ArrayElementAddressOp` — fixed in #1007
- Generator now calls `setTempVarMetadata(...makeAddressOnly...)` immediately after emission
  (`src/IrGenerator_Stmt_Decl.cpp:1735-1738`, `2674-2677`)
- Status: **generator metadata set** for the known declaration sites; unary `&arr[i]` path in
  `src/IrGenerator_Expr_Conversions.cpp:988-1011` does **not** call `setTempVarMetadata` — still a
  gap

#### `ComputeAddressOp` — fixed in #1007
- Generator now calls `setTempVarMetadata(...makeAddressOnly...)` after emission
  (`src/IrGenerator_Stmt_Decl.cpp:3096-3099`)
- Status: **generator metadata set** for the structured-binding path; the unary `&expr` lowering
  in `src/IrGenerator_Expr_Conversions.cpp:640-663` does **not** call `setTempVarMetadata` — still
  a gap

### 1C. Remaining gaps (as of Phase 2 completion)

#### `VirtualCallOp` when the callee returns `T&` or `T&&` — **fixed in Phase 2**
- Both virtual and non-virtual member-call paths now share the same post-call block at
  `src/IrGenerator_Call_Indirect.cpp:1656-1667` which calls `setTempVarMetadata(makeXValue /
  makeLValue)` and returns `makeExprResult(..., ContainsAddress)`.
- Status: **closed** — virtual T&/T&& returns are correctly annotated.

#### `ArrayAccessOp` when `optimize_lea` is true
- Backend decides at conversion time whether to keep an address or load a value
  (`src/IRConverter_ConvertMain.cpp:11830-11834`).
- The storage kind is not visible to the IR.
- Status: **converter-side only, path-dependent**

#### `MemberLoadOp` for large members (`member_size_bytes > 8`)
- Converter stores the subobject address and calls `setReferenceInfo(...)`
  (`src/IRConverter_ConvertMain.cpp:12432-12488`).
- Status: **converter-side only**; the IR generator does not know this path will be taken

#### `BinaryOp` used as pointer arithmetic (e.g. `&arr[i].member`)
- One path in `src/IrGenerator_Expr_Conversions.cpp:732-763` builds an
  `ArrayElementAddressOp` then adds a member offset with `BinaryOp Add`.
- No `setTempVarMetadata` is called on the Add result.
- Status: **gap** — the result is an address but carries no metadata

#### `AssignmentOp` with `dereference_rhs_references = false`
- Used in several places to mean "copy the pointer stored in the RHS reference, do not load the
  pointee":
  - reference identifier `LValueAddress` path (`src/IrGenerator_Expr_Primitives.cpp:950-980`)
  - local reference-variable `LValueAddress` path (`src/IrGenerator_Expr_Primitives.cpp:1129-1158`)
  - dereference-to-lvalue-reference lowering (`src/IrGenerator_Expr_Conversions.cpp:1504-1533`)
  - reference cast helper (`src/IrGenerator_NewDeleteCast.cpp:645-656`)
- The result temp usually gets `makeLValue(...)` right after, so these paths are correctly tracked
- Status: **generator metadata set** at known sites; but the `AssignmentOp` opcode itself is
  semantically ambiguous — see Option B

---

## 2. Ops that produce data values (must be materialised before reference binding)

| Producer | Status | Note |
|---|---|---|
| `CallOp` (non-reference return) | PRValue marked at gen site | Includes scalars, small structs by value |
| `VirtualCallOp` (non-reference return) | No metadata | Treated as data by default |
| `IndirectCallOp` (non-reference return) | PRValue marked | |
| `UnaryOp`, `BinaryOp` (arithmetic) | No metadata | Treated as data by default |
| `ConversionOp`, `TypeConversionOp` | No metadata | Treated as data by default |
| `DereferenceOp` (final load) | No metadata | Loads data from pointer |
| `GlobalLoadOp` (scalar) | No metadata | Data value |
| `FunctionAddressOp` | No metadata but is 64-bit pointer | Pointer **data**, not a reference slot |
| `GlobalLoadOp` (`is_array = true`) | No metadata but is 64-bit pointer | Array-decay pointer data |
| `HeapAllocOp` / `HeapAllocArrayOp` | No metadata | Heap pointer data |
| `TypeidOp` / pointer `DynamicCastOp` | No metadata | Pointer data |

**Key distinction:** a pointer-valued prvalue is still *data* for reference binding unless the IR
explicitly marks it as "already the address of the bound object".  The current `isIrPointerLikeType`
arm in `is_likely_pointer` does **not** make this distinction — a heap pointer and an
`ArrayElementAddressOp` result would both pass it.

---

## 3. Structural gap: `ExprResult` has no storage kind

Every IR-generator function returns `ExprResult` (`src/IROperandHelpers.h:82-100`), which is later
converted to `TypedValue` via `toTypedValue(ExprResult)` (`src/IROperandHelpers.h:186-195`).

Neither struct has a `ValueStorage` / `ContainsAddress` field today.

This means even if a generator has set `TempVarMetadata` on the temp inside the `ExprResult`, the
downstream conversion path at `handleVariableDecl` must look up the metadata separately.  Under
Option A, both `ExprResult` and `TypedValue` should carry the same storage kind so that the
information is never lost across the `toTypedValue(...)` boundary.

Without this, Option A is incomplete: a generator that correctly marks its output as
`ContainsAddress` at the `ExprResult` level would lose that information the moment it is converted
to a `TypedValue` with the default `LegacyUnclassified`.

---

## 4. What the current `is_likely_pointer` heuristic actually does (post-#1007)

`src/IRConverter_ConvertMain.cpp:6236-6271`:

```
1. getReferenceInfo(temp_var, src_offset) hits    → MOV
2. has_indirect_lvalue (LValueInfo::Kind::Indirect) → MOV
3. holds_address (is_address && holds_address_only)  → MOV
4. is 64-bit AND (isIrStructType OR isIrPointerLike) → MOV (heuristic arm)
5. else                                              → LEA (materialise temporary)
```

Arms 1–3 are metadata-driven and reliable.  Arm 4 is still a heuristic and introduces two
remaining failure modes:

- **False positive**: a 64-bit struct-typed prvalue (e.g. a function returning a 64-bit struct
  by value) hits `isIrStructType` and is treated as an address.
- **False positive**: a 64-bit pointer-valued data result (heap alloc, `FunctionAddressOp`, etc.)
  hits `isIrPointerLikeType` and is treated as an address of the bound object.

---

## 5. Option A — explicit `ValueStorage` on `TypedValue` and `ExprResult`

### What it changes

Add a storage discriminator to **both** `TypedValue` and `ExprResult`:

```cpp
enum class ValueStorage : uint8_t {
    LegacyUnclassified,  // migration sentinel — treat like current heuristic
    ContainsData,        // slot holds a value; reference binding must LEA or materialise
    ContainsAddress,     // slot holds address of existing object; reference binding must MOV
};
```

The critical contract:

- **`ContainsData`**: the stack slot holds a value.  Reference binding must take its address
  (LEA) or materialize a temporary.
- **`ContainsAddress`**: the stack slot already holds the 64-bit address of the object to bind.
  Reference binding must copy the stored pointer (MOV).

### Files / call sites affected

Core infrastructure (3 files):

1. `src/IRTypes_Ops.h` — add `ValueStorage storage` to `TypedValue`
2. `src/IROperandHelpers.h` — add `ValueStorage storage` to `ExprResult`; propagate in
   `makeTypedValue(...)`, `toTypedValue(ExprResult)`, `makeExprResult(...)`
3. `src/IRConverter_ConvertMain.cpp` — replace arms 2–4 of `is_likely_pointer` with direct
   storage field reads

IR-generator annotation sites (5 files, ~12-20 call sites):

4. `src/IrGenerator_Stmt_Decl.cpp` — set `ContainsAddress` on the three already-patched sites
   *and* stop relying on separate `setTempVarMetadata`; additionally annotate any new site found
5. `src/IrGenerator_Expr_Primitives.cpp` — both `AssignmentOp(dereference_rhs_references=false)`
   sites (`950-980`, `1129-1158`)
6. `src/IrGenerator_Expr_Conversions.cpp` — `ComputeAddressOp` result (`640-663`),
   `ArrayElementAddressOp` result (`988-1011`), pointer-offset `Add` result (`732-763`),
   dereference-to-reference copy (`1504-1533`)
7. `src/IrGenerator_NewDeleteCast.cpp` — address-copy helper (`645-656`)
8. `src/IrGenerator_Expr_Operators.cpp` — `buildConstructorArgumentValue` address-detection helper
   (`318-340`, `380-417`)

Remaining gap to close:

9. `src/IrGenerator_Call_Indirect.cpp` — add `ContainsAddress` annotation for `VirtualCallOp`
   when callee returns `T&` or `T&&` (currently missing — see §1C above)

### Impact on `GlobalTempVarMetadataStorage`

It does **not** need to be removed.  `TempVarMetadata` is still used for:

- value category (`LValue` / `XValue` / `PRValue`)
- `LValueInfo` for member access, compound assignment, and NRVO
- RVO / NRVO tracking

After the migration is complete, the `is_address` and `holds_address_only` fields in
`TempVarMetadata` would become redundant with `ValueStorage::ContainsAddress`.  A follow-up
cleanup (Phase 5 below) should remove them.

### Would Option A prevent the current bug class?

**Yes, if arms 1–4 are all driven by the field.**

With `LegacyUnclassified` as the initial default, any omission is immediately visible during
migration (the converter can assert or log on `LegacyUnclassified` reference-init).  Once the
migration is complete, a developer who adds a new address-producing op but forgets to annotate the
`ExprResult` / `TypedValue` will produce a `LegacyUnclassified` (or, later, a compile-time error
rather than a silent wrong-codegen).

### Migration path that keeps tests green

1. Add `ValueStorage` to `TypedValue` and `ExprResult`; default to `LegacyUnclassified`
2. Teach `handleVariableDecl` arms 1–4 to respect the field when it is not `LegacyUnclassified`;
   keep the current heuristic only for `LegacyUnclassified`
3. Annotate known address producers one file at a time (§5 list above)
4. Once all sites are annotated, replace `LegacyUnclassified` fallback with an assertion
5. Remove `TempVarMetadata.is_address` / `holds_address_only` and `setAddressOnlyInfo` /
   `setReferenceInfo` converter helpers that are now superseded by the field

---

## 6. Option B — dedicated `CopyReferenceOp` opcode

### What it changes

Replace the current pattern where `AssignmentOp` with `dereference_rhs_references = false`
means "copy the stored pointer" with a dedicated opcode:

```cpp
struct CopyReferenceOp {
    TempVar result;
    std::variant<StringHandle, TempVar> source;
    Type pointee_type = Type::Invalid;
    SizeInBits pointee_size_bits;
    bool is_rvalue_reference = false;
};
```

Converter contract: load 64-bit pointer from `source`, store to `result`, stamp
`setReferenceInfo(...)`.

### Files affected

`src/IRTypes_Core.h`, `src/IRTypes_Ops.h`, `src/IRTypes_Instructions.h`,
`src/IRConverter_ConvertMain.h/.cpp`, `src/IrGenerator_Expr_Primitives.cpp`,
`src/IrGenerator_Expr_Conversions.cpp`, `src/IrGenerator_NewDeleteCast.cpp`
(~8 files, 4 call sites).

### Does it prevent the current bug class?

**Not by itself.**  `CopyReferenceOp` only regularises the `AssignmentOp(dereference=false)`
subset.  The bugs from `ArrayElementAddressOp` / `ComputeAddressOp` / `VirtualCallOp` reference
returns are entirely outside its scope.

Option B is a valuable readability and maintainability improvement, but it is **too narrow** to be
the primary fix.

---

## 7. Recommendation

### Primary fix: Option A

**Preferred sequencing:**

1. Implement **Option A** with `LegacyUnclassified` migration sentinel
2. After Option A is complete and the heuristic fallback is gone, add **Option B** as a
   readability cleanup for the `AssignmentOp(dereference=false)` pattern

### Why not Option B first?

Because Option B only covers 4 generator-side call sites.  All address-valued `TypedValue`s that
arrive at `handleVariableDecl` from `ArrayElementAddressOp`, `ComputeAddressOp`, `VirtualCallOp`,
`MemberLoadOp`-large, `ArrayAccessOp`-optimise_lea, and pointer-arithmetic `BinaryOp` still hit
the heuristic arm.

---

## 8. Concrete implementation plan

### Phase 1: introduce explicit storage kind (infrastructure only) ✅ DONE

- ✅ Add `ValueStorage` enum to `src/IRTypes_Ops.h`
- ✅ Add `ValueStorage storage = ValueStorage::LegacyUnclassified` field to `TypedValue`
- ✅ Add `ValueStorage storage = ValueStorage::LegacyUnclassified` field to `ExprResult` in
  `src/IROperandHelpers.h`
- ✅ Propagate in `toTypedValue(ExprResult)`, `makeExprResultImpl(...)`
- ✅ Add `withStorage(ExprResult, ValueStorage)` and `withStorage(TypedValue, ValueStorage)` helpers
- ✅ Teach `handleVariableDecl` to use the field when it is not `LegacyUnclassified`:
  - `ContainsAddress` → MOV
  - `ContainsData` → LEA / materialize
  - `LegacyUnclassified` → existing heuristic + debug log warning

### Phase 2: annotate address producers ✅ DONE

Annotate all known `ContainsAddress` producers in IR generators:

| Site | Status |
|---|---|
| `IrGenerator_Stmt_Decl.cpp` (`ArrayElementAddressOp`, regular ref) | ✅ `ContainsAddress` on initializer TypedValue |
| `IrGenerator_Stmt_Decl.cpp` (`ArrayElementAddressOp`, structured binding) | ✅ done |
| `IrGenerator_Stmt_Decl.cpp` (`ComputeAddressOp`, structured binding) | ✅ done |
| `IrGenerator_Expr_Conversions.cpp` (`ArrayElementAddressOp`, unary `&`) | ✅ done |
| `IrGenerator_Expr_Conversions.cpp` (`ComputeAddressOp`, unary `&expr`) | ✅ done |
| `IrGenerator_Expr_Conversions.cpp` (`BinaryOp Add`, `&arr[i].member`) | ✅ done |
| `IrGenerator_Expr_Primitives.cpp` (`AssignmentOp(dereference=false)`, ref-param LValueAddress) | ✅ done |
| `IrGenerator_Expr_Primitives.cpp` (`AssignmentOp(dereference=false)`, ref-var LValueAddress) | ✅ done |
| `IrGenerator_Expr_Conversions.cpp` (deref-to-lvalue-reference copy) | ✅ done |
| `IrGenerator_NewDeleteCast.cpp` (reference-cast helpers) | ✅ done |
| `IrGenerator_Call_Indirect.cpp` (VirtualCallOp, `T&`/`T&&` return) — was gap in §1C | ✅ fixed; virtual + non-virtual paths share annotation |
| `IrGenerator_Call_Direct.cpp` (`T&`/`T&&` return) | ✅ done |
| `IrGenerator_Call_Indirect.cpp` (non-virtual, `T&`/`T&&` return) | ✅ done |
| `IrGenerator_MemberAccess.cpp` (reference member LValueAddress path) | ✅ done |
| `IrGenerator_MemberAccess.cpp` (struct reference member load, address not yet dereferenced) | ✅ done |
| `IrGenerator_Expr_Operators.cpp` (`buildConstructorArgumentValue` address paths) | ✅ checks `storage` field before metadata heuristic |

### Phase 3: annotate data producers ✅ DONE

Every `makeExprResult(...)`, `makeMemberResult(...)`, and `makeArrayResult(...)` call site
passes an explicit `ValueStorage` argument.  `LegacyUnclassified` has been removed from the
enum.  `handleVariableDecl` uses a direct `ContainsAddress` check — no fallback heuristic.

| Site | Status |
|---|---|
| `CallOp` / `VirtualCallOp` / `IndirectCallOp` normal returns | ✅ `ContainsData` |
| `FunctionAddressOp` result | ✅ `ContainsData` |
| `HeapAllocOp` / `HeapAllocArrayOp` result | ✅ `ContainsData` |
| `GlobalLoadOp` TempVar results | ✅ `ContainsData` |
| All arithmetic `BinaryOp` / `UnaryOp` / conversion results | ✅ `ContainsData` |
| All sizeof/alignof/noexcept/literal results | ✅ `ContainsData` |
| `emitConversionOperatorCall` result | ✅ `ContainsData` |
| `dynamic_cast` to pointer | ✅ `ContainsData` |
| `dynamic_cast` to reference | ✅ `ContainsAddress` |

### Phase 4: delete heuristic fallback ✅ DONE

`LegacyUnclassified` removed from `ValueStorage` enum.  `handleVariableDecl` reduced to
`const bool is_likely_pointer = (init.storage == ValueStorage::ContainsAddress)`.
`tempAlreadyHoldsAddress` lambda simplified to a one-liner.  All migration dead code cleaned up.

### Phase 5: legacy-path cleanup ✅ DONE (folded into Phase 4 cleanup)

`makeArrayResult` updated to require explicit `ValueStorage` storage.  `LegacyUnclassified`
references fully removed.

---

## 9. If Option B is added later

Once Option A is complete, `CopyReferenceOp` is a straightforward cleanup:

```cpp
struct CopyReferenceOp {
    TempVar result;
    std::variant<StringHandle, TempVar> source;
    Type pointee_type = Type::Invalid;
    SizeInBits pointee_size_bits;
    bool is_rvalue_reference = false;
};
```

IR-generation sites to convert first:

1. `src/IrGenerator_Expr_Primitives.cpp:965-980` — reference identifier `LValueAddress` path
2. `src/IrGenerator_Expr_Primitives.cpp:1141-1158` — local reference-variable `LValueAddress` path
3. `src/IrGenerator_Expr_Conversions.cpp:1518-1533` — dereference-to-lvalue-reference materialise
4. `src/IrGenerator_NewDeleteCast.cpp:649-656` — address-copy helper

The converter handler would: load 64-bit pointer from `source`, store to `result`,
stamp `setReferenceInfo(...)`, and set `storage = ContainsAddress`.

---

## 10. Bottom line (updated 2026-03-24)

**Original status (post-#1007):**
- The immediate bugs were fixed (#1007), but the architecture was still fragile: a new
  address-producing op required manual `setTempVarMetadata` + keeping the heuristic in sync.
- `ExprResult` and `TypedValue` lacked an explicit `ValueStorage` field.
- `VirtualCallOp` reference returns were an unaddressed gap.
- Two residual heuristic arms (`isIrStructType`, `isIrPointerLikeType`) introduced false-positive
  risks for 64-bit struct/pointer-valued data results.

**Current status (all phases complete):**
- `ValueStorage` has two values: `ContainsData` and `ContainsAddress`.  `LegacyUnclassified`
  is gone.
- `ValueStorage storage` is a **required parameter** of `makeExprResult()`, `makeMemberResult()`,
  and `makeArrayResult()`.  Omitting it is a **compile error**.
- `handleVariableDecl` uses a single `ContainsAddress` check — no fallback, no heuristic, no
  TempVar metadata lookup for the MOV-vs-LEA decision.
- `tempAlreadyHoldsAddress` in `IrGenerator_Expr_Operators.cpp` is a one-liner.

**Remaining work:**
1. Option B: add `CopyReferenceOp` opcode as readability cleanup (see §9).
   (`TempVarMetadata.is_address`/`holds_address_only`/`setReferenceInfo`/`setAddressOnlyInfo`
   are still live — they drive `indirect_stack_info_` for parameter and 'this' setup.)
2. Phase 6/7: consolidate reference-tracking inconsistencies (see §11).

---

## 11. Remaining inconsistencies in `indirect_stack_info_` / TempVar metadata (Phase 6/7 plan)

Option A (Phases 1–5) cleaned up the IR-generator layer.  Two lower-level inconsistencies
remain inside the converter's `indirect_stack_info_` / `TempVarMetadata` dual-tracking system
that could cause bugs as the compiler grows.

---

### 11.1 — Bug risk: T&& call-return `holds_address_only` flag is wrong in the stack map

**Location:** `src/IRConverter_ConvertMain.cpp:4493-4497`

```cpp
if (call_op.returns_rvalue_reference) {
    setIndirectStorageInfo(result_offset, ..., is_rvalue_ref=true, holds_address_only=true, TempVar{0});
}
```

For T&& (`rvalue reference`) non-virtual call returns, the converter registers the result slot in
`indirect_stack_info_` with `holds_address_only=true` ("raw pointer, do not implicitly deref").

But the generator's shared post-call block (`IrGenerator_Call_Indirect.cpp:1662-1663`) also calls:

```cpp
setTempVarMetadata(ret_var, TempVarMetadata::makeXValue(lvalue_info, ...))
```

`makeXValue` sets `is_address=true, holds_address_only=false` — a **true reference** that
*should* be implicitly dereffed when used as an rvalue.

These two registrations are contradictory:

| Lookup method | Result |
|---|---|
| `getReferenceInfo(ret_var, result_offset)` | TempVar checked first → `holds_address_only=false` (correct) |
| `getIndirectStackInfo(result_offset)` (naked offset) | Stack map only → `holds_address_only=true` (wrong) |

There are ~25 callers of `getIndirectStackInfo(offset)` (naked offset, no TempVar) in the
converter.  Code paths that query the T&& result slot by offset alone will see
`holds_address_only=true` and NOT dereference when they should.  For example, the non-reference
struct-init path at line 6433 would copy the 64-bit pointer value instead of loading the object
through it.

**Fix (Phase 6):** Remove lines 4493-4497 entirely.

The generator already registers the T&& result in TempVar metadata correctly.  Every code path
that holds the TempVar can use `getReferenceInfo(ret_var, result_offset)` and get the right
answer.  The only callers of naked `getIndirectStackInfo(result_offset)` for T&& results would
then get `nullopt`, which triggers the "not a reference, copy raw value" path — also correct,
since T&& results are 64-bit addresses and copying the 64-bit value gives the address.

**Before implementing:** add a test that returns T&& from a non-virtual function and then
initializes both a `T&&` reference variable and a `T` copy variable from that result, to
confirm the two different code paths produce correct machine code.

---

### 11.2 — Redundancy: dual-tracking for non-zero TempVar `setReferenceInfo` calls

When `setReferenceInfo(offset, ..., result_var)` or `setAddressOnlyInfo(offset, ..., result_var)`
is called with a **non-zero** `result_var`, `setIndirectStorageInfo` writes to both:
1. `indirect_stack_info_[offset]` (by stack offset)
2. `TempVarMetadata` for `result_var` (via `makeReference`/`makeAddressOnly`)

`getReferenceInfo(result_var, offset)` always checks TempVar metadata first, so the
`indirect_stack_info_` entry is a dead backup whenever the caller supplies the TempVar.

Affected call sites (all pass non-zero TempVar):

| Location | Op |
|---|---|
| `IRConverter_ConvertMain.cpp:11827` | `handleArrayAccessOp` `optimize_lea=true` |
| `IRConverter_ConvertMain.cpp:12481` | `handleMemberLoad` large member (>8 bytes) |
| `IRConverter_ConvertMain.cpp:12629` | `handleMemberLoad` reference member |
| `IRConverter_ConvertMain.cpp:14022` | Catch-clause exception reference |
| `IRConverter_ConvertMain.cpp:14082` | Catch-clause exception reference |
| `IRConverter_ConvertMain.cpp:14358` | Catch-clause exception reference |
| `IRConverter_ConvertMain.cpp:13201` | `handleAddressOf` (`setAddressOnlyInfo`) |
| `IRConverter_ConvertMain.cpp:13309` | `handleAddressOfMember` (`setAddressOnlyInfo`) |

The stack-offset entries are redundant (TempVar always wins), but each entry consumes memory
and creates a maintenance burden: future changes to `setIndirectStorageInfo` must reason about
both paths.

**Fix (Phase 7):** In `setIndirectStorageInfo`, only write to `indirect_stack_info_` when
`temp_var.var_number == 0`.

```cpp
// Only named variables (params, 'this', declared refs) need the stack-offset map.
// TempVar results are tracked exclusively via GlobalTempVarMetadataStorage.
if (temp_var.var_number == 0) {
    indirect_stack_info_[stack_offset] = IndirectStorageInfo{ ... };
}
```

**Before implementing:** Audit every `getIndirectStackInfo(offset)` caller (naked offset,
~25 sites) to confirm none queries a TempVar-based result slot by offset alone.  The critical
question per site: "is this offset always a named-variable slot, or could it be a TempVar slot?"

Sites known to only access named-variable offsets: assignment LHS/RHS lookups by `StringHandle`
name, function-parameter registration, 'this' offset.  These are safe.

Sites that might access TempVar offsets: struct-init path at line 6433
(`getIndirectStackInfo(src_offset)` where `src_offset` came from `getStackOffsetFromTempVar`).
This site needs per-case analysis before Phase 7 can proceed.

---

### 11.3 — Final architecture after Phases 6 + 7

Once both phases are complete, the reference-tracking ownership becomes clean and orthogonal:

| What is tracked | How it is tracked |
|---|---|
| Reference parameters | `indirect_stack_info_` only (TempVar{0} caller) |
| `this` pointer | `indirect_stack_info_` only (TempVar{0} caller) |
| Reference local variable declarations | `indirect_stack_info_` only (TempVar{0} caller) |
| TempVar results (all op types) | `TempVarMetadata` only |

No dual-tracking.  `getReferenceInfo(temp_var, offset)` can be simplified: try TempVar metadata
for non-zero TempVars, try stack map for named variables.  The current fallback chain (try
TempVar → try stack map) is only needed while both can hold entries for the same slot.

---

### 11.4 — Relationship to Option B (`CopyReferenceOp`)

After Phases 6 + 7, the `setReferenceInfo` calls still needed are:
- `TempVar{0}` calls for parameters/this → stay in `setReferenceInfo`, driven by `FunctionDeclOp`
- `handleArrayAccessOp` (optimize_lea) → still converter-internal, no IR representation
- `handleMemberLoad` large member → still converter-internal
- Catch-clause refs → driven by the catch opcode handler

Option B (`CopyReferenceOp`) would give the remaining TempVar-based reference *copies* a
dedicated IR opcode.  This would consolidate the scattered `setReferenceInfo(offset, ...,
result_var)` calls in `handleMemberLoad` / `handleArrayAccess` into a single opcode handler,
at the cost of adding a new IR instruction type.

---

## 12. Bug discovered and fixed while auditing §11 — `static_cast<T&&>(global)` crash

While adding a regression test for §11.1 (T&& call-return tracking), a runtime crash was
discovered: a function returning `T&&` where the bound object is a **global variable** produced
wrong machine code that dereferenced the struct's *value bits* as a pointer, causing SIGSEGV.

### Root cause

Two cooperating functions in `IrGenerator_NewDeleteCast.cpp`:

1. `extractBaseOperand(expr_operands, ...)` — extracts the "base" that `generateAddressOfForReference`
   should take the address of.  For any `TempVar`-valued `ExprResult`, it returned the TempVar
   unconditionally.

2. `generateAddressOfForReference(base, ...)` — when `base` is a `TempVar`, assumed "source is
   TempVar — it already holds an address" and emitted `AssignmentOp(dereference_rhs=false)` to
   copy the 64-bit pointer from that TempVar.

For `static_cast<Pair&&>(g_pair)`:

- `g_pair` was loaded via `GlobalLoadOp`, putting the struct's **value** (64-bit data) in a
  TempVar.
- `extractBaseOperand` returned that TempVar.
- `generateAddressOfForReference` emitted `AssignmentOp` that copied the 64-bit **value** into
  the result, treating it as a pointer.
- `handleReturn` then returned the VALUE bits in RAX, which the caller stored as the T&&
  reference pointer.  Reading through it → SIGSEGV.

Additionally, `handleReturn`'s switch over `LValueInfo::Kind` lacked a `Global` case, so even
if the fix above hadn't been applied, the fallback path would return the wrong 64-bit value.

### Fix

1. **`extractBaseOperand`** (`src/IrGenerator_NewDeleteCast.cpp`): when the source TempVar has
   `LValueInfo::Kind::Global`, return the global name (`StringHandle`) rather than the TempVar.
   `generateAddressOfForReference` then emits `AddressOfOp(global_name)`, which the converter
   handles with a RIP-relative LEA, correctly yielding the global's runtime address.

2. **`handleReturn`** (`src/IRConverter_ConvertMain.cpp`): added `case LValueInfo::Kind::Global`
   to the reference-return switch.  Emits `LEA RAX, [RIP + global_name]` via
   `emitLeaRipRelative` + `pending_global_relocations_`, so that T& / T&& returns whose
   LValueInfo resolves to a global are handled even if the generator path didn't go through
   `generateAddressOfForReference`.

### Test

`tests/test_rval_ref_return_ret0.cpp` — covers:
- Non-virtual function returning `int&&` by forwarding a T&& parameter
- Non-virtual function returning `Pair&&` from a global via `static_cast<Pair&&>`
- Binding the T&& result to a T&& reference variable
- Mutating the referent through the bound reference and checking object identity
