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

### 1C. Still not tracked — remaining gaps

#### `VirtualCallOp` when the callee returns `T&` or `T&&`
- IR generation never calls `setTempVarMetadata` on the ret_var after emitting `VirtualCallOp`
  (`src/IrGenerator_Call_Indirect.cpp:1018-1087`).
- The non-virtual member-call branch (same file, line 1656-1666) *does* set metadata, but the
  early-exit `is_virtual_call` branch at line 1018 does not.
- Status: **gap** — virtual reference returns fall through to the `isIrStructType /
  isIrPointerLikeType` heuristic in `handleVariableDecl`

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

### Phase 1: introduce explicit storage kind (infrastructure only)

- Add `ValueStorage` enum to `src/IRTypes_Ops.h`
- Add `ValueStorage storage = ValueStorage::LegacyUnclassified` field to `TypedValue`
- Add `ValueStorage storage = ValueStorage::LegacyUnclassified` field to `ExprResult` in
  `src/IROperandHelpers.h`
- Propagate in `makeTypedValue(...)`, `toTypedValue(ExprResult)`, `makeExprResult(...)`
- Teach `handleVariableDecl` to use the field when it is not `LegacyUnclassified`:
  - `ContainsAddress` → MOV
  - `ContainsData` → LEA / materialize
  - `LegacyUnclassified` → existing heuristic + log warning

### Phase 2: annotate address producers

Annotate all known `ContainsAddress` producers in IR generators:

| Site | Status |
|---|---|
| `IrGenerator_Stmt_Decl.cpp:1735` (`ArrayElementAddressOp`, regular ref) | manual `setTempVarMetadata` already present — replace with storage field |
| `IrGenerator_Stmt_Decl.cpp:2674` (`ArrayElementAddressOp`, structured binding) | same |
| `IrGenerator_Stmt_Decl.cpp:3096` (`ComputeAddressOp`, structured binding) | same |
| `IrGenerator_Expr_Conversions.cpp:988-1011` (`ArrayElementAddressOp`, unary `&`) | **gap** — add annotation |
| `IrGenerator_Expr_Conversions.cpp:640-663` (`ComputeAddressOp`, unary `&expr`) | **gap** — add annotation |
| `IrGenerator_Expr_Conversions.cpp:732-763` (`BinaryOp Add`, `&arr[i].member`) | **gap** — add annotation |
| `IrGenerator_Expr_Primitives.cpp:950-980` (`AssignmentOp(dereference=false)`, ref-param LValueAddress) | metadata already set via `makeLValue` — annotate storage field to match |
| `IrGenerator_Expr_Primitives.cpp:1129-1158` (`AssignmentOp(dereference=false)`, ref-var LValueAddress) | same |
| `IrGenerator_Expr_Conversions.cpp:1504-1533` (deref-to-lvalue-reference copy) | add annotation |
| `IrGenerator_NewDeleteCast.cpp:645-656` (address-copy helper) | add annotation |
| `IrGenerator_Call_Indirect.cpp` (VirtualCallOp, `T&`/`T&&` return) | **gap** — add both `setTempVarMetadata` and storage annotation |
| `IrGenerator_Expr_Operators.cpp:318-340, 380-417` (`buildConstructorArgumentValue`) | add annotation for paths that produce address results |

### Phase 3: annotate data producers

Mark the key data-valued producers as `ContainsData` so the fallback heuristic is never needed for
them:

- `CallOp` normal returns (already marked `PRValue` — set `ContainsData` to match)
- `VirtualCallOp` normal returns
- `IndirectCallOp` normal returns
- arithmetic `BinaryOp` / `UnaryOp` / conversion results
- `GlobalLoadOp` scalar
- `HeapAllocOp` / `HeapAllocArrayOp` / `PlacementNewOp`
- `FunctionAddressOp` (64-bit pointer data — must **not** be `ContainsAddress`)
- `GlobalLoadOp` with `is_array = true` (array-decay pointer data)

### Phase 4: delete heuristic fallback

- Replace the `LegacyUnclassified` branch in `handleVariableDecl` with an `InternalError` /
  `assert`
- Delete `isIrStructType(init_ir)` and `isIrPointerLikeType(init_ir)` arms
- Delete `is_likely_pointer` variable entirely

### Phase 5: consolidate redundant metadata (optional cleanup)

After Phase 4, `TempVarMetadata.is_address`, `TempVarMetadata.holds_address_only`,
`setAddressOnlyInfo`, and `setReferenceInfo` in `IRConverter_ConvertMain.cpp` are redundant with
`ValueStorage`.  Remove them and update the converter helpers that read them.

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

## 10. Bottom line

- The immediate bugs are fixed (#1007), but the architecture is still fragile: a new
  address-producing op requires manual `setTempVarMetadata` + keeping the heuristic in sync.
- `ExprResult` and `TypedValue` both need an explicit `ValueStorage` field; omitting `ExprResult`
  would leave a loss-of-information gap in `toTypedValue(...)`.
- `VirtualCallOp` reference returns are an unaddressed gap — the only current call path that
  produces a reference result but never sets `TempVarMetadata`.
- Two residual heuristic arms (`isIrStructType`, `isIrPointerLikeType`) remain after #1007 and
  introduce false-positive risks for 64-bit struct/pointer-valued data results.
- The complete fix is Option A (storage field on both `ExprResult` and `TypedValue`), followed
  optionally by Option B as a readability cleanup.

**Recommendation: implement Option A with `LegacyUnclassified` migration sentinel.
Address `ExprResult` and `VirtualCallOp` gaps from the start; they are the two sites most likely
to produce the next bug of this class.**
