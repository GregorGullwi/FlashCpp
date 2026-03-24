# TempVar / local-variable IR architecture proposal (2026-03-24)

## Scope

This note audits the current TempVar/local-variable IR architecture around reference binding and address-producing temporaries.

Files inspected for this proposal:

- `/src/IRTypes_Registers.h` (`TempVarMetadata`, `ValueCategory`, `LValueInfo`)
- `/src/IRTypes_Ops.h` (`TypedValue` and all relevant op structs)
- `/src/IRConverter_ConvertMain.cpp` (especially `handleVariableDecl`, `setIndirectStorageInfo`, `handleArrayAccess`, `handleArrayElementAddress`, `handleMemberLoad`, `handleAddressOf`, `handleAddressOfMember`, `handleComputeAddress`, and call handling)
- `/src/IrGenerator_Stmt_Decl.cpp`
- `/src/IrGenerator_Expr_Primitives.cpp`
- `/src/IrGenerator_Expr_Conversions.cpp`
- `/src/IrGenerator_Call_Direct.cpp`
- `/src/IrGenerator_Call_Indirect.cpp`
- `/src/IrGenerator_MemberAccess.cpp`
- `/src/IROperandHelpers.h`
- `/src/IrGenerator_Expr_Operators.cpp`

## Executive summary

The current design has **two separate sources of truth** for "does this TempVar stack slot already hold an address?":

1. `TempVarMetadata` / `GlobalTempVarMetadataStorage` in the IR generator (`src/IRTypes_Registers.h:243-353`, `src/IRTypes_Ops.h:246-339`)
2. `indirect_stack_info_` in the converter (`src/IRConverter_ConvertMain.cpp:1752-1797`, `1800-1897`)

`handleVariableDecl` then has to bridge the gap with a heuristic when the initializer is a `TempVar` that has no recorded indirect-storage info (`src/IRConverter_ConvertMain.cpp:6221-6252`).

That heuristic is fundamentally fragile because it guesses from **size and IR type**:

```cpp
bool is_likely_pointer = (init.size_in_bits == SizeInBits{64} &&
                          (isIrIntegerType(init_ir) || isIrStructType(init_ir) || isIrPointerLikeType(init_ir)));
```

The architectural problem is therefore not just one bad condition. The real problem is that the IR does not carry a single, authoritative fact for:

- "this TempVar contains a data value"
- "this TempVar contains the address of another object"

As long as that fact is reconstructed post-hoc, new address-producing ops will keep being bug-prone.

## Clarification relative to REVIEW.md

`REVIEW.md` asks whether code generation is doing lookups or fallbacks because semantic analysis failed to model something earlier (`/home/runner/work/FlashCpp/FlashCpp/REVIEW.md:14`).

For this issue, I **do not** think `IRConverter` reading `TempVarMetadata` / `LValueInfo` is itself a sema-layer problem. The codebase already uses the pattern `setTempVarMetadata(...)` in IR generation and `getTempVarMetadata(...)` / `getTempVarLValueInfo(...)` later in codegen (`src/IRTypes_Ops.h:246-267`; for example `src/IRConverter_ConvertMain.cpp:8198-8200`, `src/IrGenerator_Expr_Operators.cpp:4183-4253`, `src/IrGenerator_MemberAccess.cpp:1300-1325`). That is an established **codegen-to-codegen metadata contract**.

The architectural problem here is narrower and more concrete:

- the same "this temp already holds an address" fact is represented in multiple codegen-layer channels
- some paths seed only TempVar metadata
- some paths seed only converter-side indirect-storage info
- and `handleVariableDecl` still falls back to `is_likely_pointer` when neither channel has the fact

So the objection is **not** "IRConverter must not read metadata." The objection is "address storage is not represented once, authoritatively, in the IR path that reference initialization consumes."

## Important discrepancy vs. the issue description

The issue description refers to three `IrGenerator_Stmt_Decl.cpp` sites that "now call `setTempVarMetadata(...)` after emitting address ops".

In the current checkout I audited, those `setTempVarMetadata(...)` calls are **not present** in the requested declaration-site ranges:

- `/src/IrGenerator_Stmt_Decl.cpp:1563-1615`
- `/src/IrGenerator_Stmt_Decl.cpp:2525-2565`
- `/src/IrGenerator_Stmt_Decl.cpp:2943-2967`

Those sites still emit `ArrayElementAddressOp` / `ComputeAddressOp`, wrap the resulting temp as a raw 64-bit `TypedValue`, and rely on downstream code to understand that it is an address.

That discrepancy does not change the architectural conclusion; it reinforces it. If a local patch exists elsewhere that adds `setTempVarMetadata(...)` there, that patch is still only another manual synchronization point.

## Current architecture in one paragraph

- `TypedValue` carries semantic/runtime type info, but **no storage-kind bit** (`src/IRTypes_Ops.h:401-429`).
- `TempVarMetadata` tracks value category, lvalue location, and two indirect-storage flags: `is_address` and `holds_address_only` (`src/IRTypes_Registers.h:243-353`).
- The converter mirrors a similar concept in `IndirectStorageInfo` and exposes it through `setReferenceInfo`, `setAddressOnlyInfo`, `getIndirectStackInfo`, `isPointerBaseStorage`, and `getReferenceInfo` (`src/IRConverter_ConvertMain.cpp:1752-1897`).
- Some ops stamp TempVar metadata in the IR generator, some stamp indirect-storage info in the converter, some do both, and some do neither.
- When `VariableDeclOp` initializes a local reference from a `TempVar`, the converter first checks `getReferenceInfo(...)`; if that fails, it falls back to `is_likely_pointer` and guesses whether to `MOV` or `LEA` (`src/IRConverter_ConvertMain.cpp:6221-6252`).

## 1. Ops whose TempVar stack slot can hold an address rather than a data value

The table below is intentionally split into three categories:

- **Address slot (true reference)**: stack slot holds an address that should usually be implicitly dereferenced
- **Address slot (address-only)**: stack slot holds an address/pointer value that should *not* be implicitly dereferenced
- **Address slot today, but inferred by convention/heuristic**: the slot behaves like an address, but the fact is not carried cleanly

### 1A. Explicit address-only producers

#### `AddressOfOp`
- Definition: `src/IRTypes_Ops.h:591-594`
- Converter behavior: computes the address, stores a 64-bit pointer, then calls `setAddressOnlyInfo(...)` (`src/IRConverter_ConvertMain.cpp:13130-13208`)
- Metadata status: **set in converter only**, not in the op/result type itself
- Current semantics: plain pointer/address, **not** a true C++ reference

#### `AddressOfMemberOp`
- Definition: `src/IRTypes_Ops.h:597-603`
- Converter behavior: computes the member address, stores a 64-bit pointer, then calls `setAddressOnlyInfo(...)` (`src/IRConverter_ConvertMain.cpp:13271-13315`)
- Metadata status: **set in converter only**
- Current semantics: plain pointer/address, **not** a true C++ reference

### 1B. Explicit reference-like address producers

#### `CallOp` when the callee returns `T&` or `T&&`
- Definition: `src/IRTypes_Ops.h:471-494`
- IR-generation metadata:
  - direct call path sets `makeLValue(...)` / `makeXValue(...)` on `ret_var` (`src/IrGenerator_Call_Direct.cpp:1588-1598`)
  - member/indirect call path does the same (`src/IrGenerator_Call_Indirect.cpp:1656-1666`)
- Converter behavior:
  - for `T&&`, converter additionally calls `setIndirectStorageInfo(..., holds_address_only=true, ...)` (`src/IRConverter_ConvertMain.cpp:4491-4498`)
  - for `T&`, converter does **not** add equivalent stack-side tracking there
- Metadata status: **mixed**; front-end metadata exists, converter-side storage info is only partially mirrored
- Architectural note: this already shows the dual-source-of-truth problem

#### `MemberLoadOp` when the member itself is a reference member
- Definition: `src/IRTypes_Ops.h:497-511`
- IR-generation metadata:
  - member access attaches `makeLValue(...)` / `makeXValue(...)` metadata to the result temp (`src/IrGenerator_MemberAccess.cpp:1329-1346`)
  - in `LValueAddress` context, reference members are rewritten to `Kind::Indirect` (`src/IrGenerator_MemberAccess.cpp:1378-1388`)
  - in load context, reference members are dereferenced and the dereferenced temp is again marked indirect-lvalue (`src/IrGenerator_MemberAccess.cpp:1392-1421`)
- Converter behavior:
  - if `op.is_reference()`, member load stores the 64-bit pointer and calls `setReferenceInfo(...)` (`src/IRConverter_ConvertMain.cpp:12632-12636`)
- Metadata status: **set in both places**

#### `ArrayAccessOp` when `optimize_lea` is true
- Definition: `src/IRTypes_Ops.h:559-567`
- Converter behavior:
  - for lvalue/struct-style access, it keeps the computed address in a register and stores that 64-bit address to the temp slot (`src/IRConverter_ConvertMain.cpp:11671-11828`)
  - then calls `setReferenceInfo(...)` only when `optimize_lea` is true (`src/IRConverter_ConvertMain.cpp:11830-11834`)
- Metadata status: **set in converter only and only on the LEA path**
- Architectural note: a single opcode can produce either data or address depending on a backend-side decision

#### `MemberLoadOp` for large members (`member_size_bytes > 8`)
- Definition: `src/IRTypes_Ops.h:497-511`
- Converter behavior: instead of loading the whole object, it stores the computed address of the subobject and calls `setReferenceInfo(...)` (`src/IRConverter_ConvertMain.cpp:12432-12488`)
- Metadata status: **set in converter only**
- Architectural note: again, the opcode result shape depends on converter logic rather than the IR type system

### 1C. Explicit address producers that are currently under-described or untracked

#### `ArrayElementAddressOp`
- Definition: `src/IRTypes_Ops.h:581-588`
- Converter behavior: computes element address and stores a 64-bit pointer (`src/IRConverter_ConvertMain.cpp:11841-11964`)
- Metadata status: **not set in the converter at this opcode site**
- Current declaration-site uses:
  - local reference initialization from `arr[i]` (`src/IrGenerator_Stmt_Decl.cpp:1563-1605`)
  - structured-binding reference to array element (`src/IrGenerator_Stmt_Decl.cpp:2525-2544`)
  - unary `&arr[i]` and related lowering (`src/IrGenerator_Expr_Conversions.cpp:988-1011`)
- Architectural note: this is exactly the kind of op that later forces `handleVariableDecl` to guess

#### `ComputeAddressOp`
- Definition: `src/IRTypes_Ops.h:606-626`
- Converter behavior: computes `base + scaled indices + member offset`, stores a 64-bit pointer (`src/IRConverter_ConvertMain.cpp:13324-13444`)
- Metadata status: **not set in the converter at this opcode site**
- Current declaration-site use:
  - structured-binding reference to member (`src/IrGenerator_Stmt_Decl.cpp:2943-2967`)
- Other generation site:
  - one-pass lowering for unary `&expr` (`src/IrGenerator_Expr_Conversions.cpp:640-663`)
- Architectural note: another direct source of address-valued temps with no intrinsic storage tag

#### `BinaryOp` used as address arithmetic
- Definition: `src/IRTypes_Ops.h:448-452`
- Not every `BinaryOp` is address-producing, but at least one current path uses it that way:
  - `&arr[i].member` builds `ArrayElementAddressOp` and then a pointer-offset `Add` into a new `TempVar` (`src/IrGenerator_Expr_Conversions.cpp:732-763`)
- Metadata status: **none at the op site shown above**
- Architectural note: this is important for option comparison. If the compiler can synthesize addresses through generic arithmetic ops, any solution that only cleans up one dedicated opcode family is incomplete.

### 1D. Existing "copy the stored address" producers built out of `AssignmentOp`

These are not dedicated op structs today, but they are crucial because they motivate Option B.

#### `AssignmentOp` with `dereference_rhs_references = false`
- Definition: `src/IRTypes_Ops.h:691-697`
- Current meaning in several places: "copy the pointer stored in the RHS reference/temp into the result temp"
- Important sites:
  - reference identifier `LValueAddress` path (`src/IrGenerator_Expr_Primitives.cpp:950-980`)
  - local reference-variable `LValueAddress` path (`src/IrGenerator_Expr_Primitives.cpp:1129-1158`)
  - dereference-to-lvalue-reference lowering (`src/IrGenerator_Expr_Conversions.cpp:1504-1533`)
  - reference cast helper when source already holds an address (`src/IrGenerator_NewDeleteCast.cpp:645-656`)
- Metadata status: the resulting temp usually gets `makeLValue(...)` immediately after emission in the IR generator, but the opcode itself is semantically ambiguous
- Architectural note: this is the best argument *for* Option B, but it is not the only bug surface in the current architecture

## 2. Ops that produce data values which must be materialized before reference binding

These are the ops/results that are **not** addresses of an existing object. If a reference is allowed to bind to them, the compiler must create a temporary object/stack slot and then bind the reference to that temporary.

### 2A. Always-data producers

#### `CallOp` for ordinary non-reference returns
- Definition: `src/IRTypes_Ops.h:471-494`
- Direct-call IR generation marks normal call results as PRValues (`src/IrGenerator_Call_Direct.cpp:217-218`, `345-346`, `984`)
- Converter stores the returned register value to the TempVar slot (`src/IRConverter_ConvertMain.cpp:4455-4484`)
- This includes scalar returns and small struct-by-value returns
- These results must be materialized if a reference binds to them

#### `IndirectCallOp`
- Definition: `src/IRTypes_Ops.h:932-936`
- IR generation for function-pointer calls just returns the TempVar result (`src/IrGenerator_Call_Direct.cpp:213-250`)
- Converter stores the return register to the temp slot as data (`src/IRConverter_ConvertMain.cpp:13863-13910` plus store at `5469-5474` for virtual; indirect call follows the same pattern later in the function)
- There is no reference-return-specific storage handling in the op itself

#### `VirtualCallOp`
- Definition: `src/IRTypes_Ops.h:661-669`
- IR generation packages the result as `TypedValue`, but I found no dedicated storage-kind bit (`src/IrGenerator_Call_Indirect.cpp:1018-1059`)
- Converter stores RAX into the result slot (`src/IRConverter_ConvertMain.cpp:5469-5474`)
- If a virtual call returns a by-value scalar or pointer, that is data and must be materialized for reference binding

#### `UnaryOp`
- Definition: `src/IRTypes_Ops.h:760-763`
- No address semantics; result is a computed value

#### `ConversionOp`
- Definition: `src/IRTypes_Ops.h:792-797`
- No address semantics; result is a converted value

#### `TypeConversionOp`
- Definition: `src/IRTypes_Ops.h:909-914`
- No address semantics; result is a converted value

#### `BinaryOp` in its normal arithmetic/comparison role
- Definition: `src/IRTypes_Ops.h:448-452`
- No address semantics unless a specific lowering path intentionally uses it as pointer arithmetic (see §1C)

#### `DereferenceOp` when it performs the final load
- Definition: `src/IRTypes_Ops.h:629-632`
- Converter loads through the pointer and stores the loaded value (`src/IRConverter_ConvertMain.cpp:13447 onward`)
- In other words, the result is usually the data, not the address, unless the IR generator has already shifted the expression into an indirect-lvalue form

#### `GlobalLoadOp` for ordinary global scalars
- Definition: `src/IRTypes_Ops.h:800-809`
- Converter loads the value via RIP-relative MOV / MOVSS / MOVSD and stores the value to the temp slot (`src/IRConverter_ConvertMain.cpp:6070-6112`)

### 2B. Pointer-typed results that are still data for this problem

This is the subtle but important distinction.

A pointer-typed prvalue is still **data** for reference binding unless the IR means "this slot already holds the address of the object that should be bound".

Examples:

#### `FunctionAddressOp`
- Definition: `src/IRTypes_Ops.h:812-825`
- Converter uses `LEA` to get the function symbol address and stores that pointer value to the temp slot (`src/IRConverter_ConvertMain.cpp:13823-13859`)
- No indirect-storage metadata is recorded there
- For the reference-binding problem, this is pointer **data**, not a reference/address slot

#### `GlobalLoadOp` with `is_array = true`
- Definition: `src/IRTypes_Ops.h:800-809`
- Converter uses `LEA` and stores a 64-bit pointer (`src/IRConverter_ConvertMain.cpp:6087-6110`)
- No `setAddressOnlyInfo(...)` call is made here
- Semantically this is array-decay pointer data, not "reference to the global array object"

#### `HeapAllocOp`, `HeapAllocArrayOp`, `PlacementNewOp`
- Definitions: `src/IRTypes_Ops.h:871-906`
- Converter stores the returned heap/placement pointer to the temp slot (`src/IRConverter_ConvertMain.cpp:5481-5626`, `5722-5768`)
- These are pointer-valued data results

#### `TypeidOp` / pointer-form `DynamicCastOp`
- Definitions: `src/IRTypes_Ops.h:917-929`
- Converter produces pointer results and stores them as 64-bit values (`src/IRConverter_ConvertMain.cpp:5772-5842`, `5845 onward`)
- These are pointer-valued data results

This distinction matters because the current `is_likely_pointer` whitelist does **not** distinguish "pointer-valued data" from "TempVar that already names the bound object by address".

## 3. What the current `is_likely_pointer` heuristic actually does

The relevant code is in `/src/IRConverter_ConvertMain.cpp:6221-6252`.

Flow for local reference initialization from a `TempVar`:

1. If the source TempVar already has `getReferenceInfo(...)`, the converter uses `MOV` (`6230-6235`)
2. Otherwise it computes:
   - `init_ir = init.effectiveIrType()`
   - `is_likely_pointer = init.size_in_bits == 64 && (isIrIntegerType(init_ir) || isIrStructType(init_ir) || isIrPointerLikeType(init_ir))` (`6242-6244`)
3. If `is_likely_pointer` is true, it uses `MOV` (`6246-6248`)
4. Otherwise it uses `LEA` (`6249-6251`)

Why this is fragile:

- 64-bit integer/pointer/struct-ish values are grouped together even though they do not mean the same thing for reference binding
- pointer-valued data and address-of-object temps are conflated
- any new address-producing opcode that forgets to seed metadata is silently routed through this guesswork
- any new pointer-valued data opcode can also be misclassified the other way

## 4. Option A — typed TempVar slot (`ValueStorage` on `TypedValue`)

## What it would change

Add a storage discriminator such as:

```cpp
enum class ValueStorage {
ContainsData,
ContainsAddress,
};
```

on `TypedValue` (`src/IRTypes_Ops.h:401-429`), with helper constructors in `src/IROperandHelpers.h:133-195`.

The critical rule would be:

- `ContainsData`: the slot contains a value; reference binding must materialize/take address as needed
- `ContainsAddress`: the slot already contains the address of the object to bind/store through; reference binding must load/copy the stored pointer

In practice I would strongly prefer a temporary migration state of:

```cpp
enum class ValueStorage {
LegacyUnclassified,
ContainsData,
ContainsAddress,
};
```

with `handleVariableDecl` asserting or logging on `LegacyUnclassified` once the migration is underway.

## Files / call sites likely affected

Minimum core files:

1. `src/IRTypes_Ops.h` — add the field to `TypedValue`
2. `src/IROperandHelpers.h` — set defaults in `makeTypedValue(...)` / `toTypedValue(...)`
3. `src/IRConverter_ConvertMain.cpp` — replace `is_likely_pointer` with direct storage inspection in `handleVariableDecl`

Then the main address-producing packaging sites:

4. `src/IrGenerator_Stmt_Decl.cpp`
   - reference init from `ArrayElementAddressOp` (`1563-1605`)
   - structured-binding array refs (`2525-2544`)
   - structured-binding member refs (`2943-2967`)
5. `src/IrGenerator_Expr_Primitives.cpp`
   - both `AssignmentOp(... dereference_rhs_references = false)` sites (`950-980`, `1129-1158`)
6. `src/IrGenerator_Expr_Conversions.cpp`
   - `ComputeAddressOp` result packaging (`640-663`)
   - `ArrayElementAddressOp` result packaging (`988-1011`)
   - pointer-offset `BinaryOp` address path for `&arr[i].member` (`732-763`)
   - dereference-to-reference copy path (`1504-1533`)
7. `src/IrGenerator_NewDeleteCast.cpp`
   - reference cast helper that copies an already-computed address (`645-656`)
8. `src/IrGenerator_Expr_Operators.cpp`
   - constructor/reference argument materialization helper currently re-checks `TempVarMetadata` and falls back to size/type guesses (`318-340`, `380-417`)

Realistically this is **8 files** and on the order of **12-20 targeted call sites**, not a repository-wide rewrite.

## Impact on `GlobalTempVarMetadataStorage`

It does **not** need to be removed.

`GlobalTempVarMetadataStorage` is still useful for:

- value category (`LValue` / `XValue` / `PRValue`)
- `LValueInfo`
- NRVO/RVO metadata
- indirect-lvalue behavior for compound assignment and member access

Option A only removes the need to abuse that metadata as the *sole* carrier of "this temp slot contains an address".

## Would Option A prevent the current bug class?

**Yes, if implemented strictly.**

It directly attacks the real failure mode: the converter would stop inferring address-ness from backend heuristics and would instead read an IR-authored fact.

However, the migration should be designed so omissions are visible. If `ValueStorage` defaults silently to `ContainsData`, a missed annotation could still regress correctness. That is why `LegacyUnclassified` during migration is safer than going straight to a silent default.

## Migration path that can keep tests green

A safe incremental path is:

1. Add `ValueStorage::LegacyUnclassified/ContainsData/ContainsAddress` to `TypedValue`
2. Default all existing helper-created `TypedValue`s to `LegacyUnclassified`
3. Teach `handleVariableDecl` to:
   - prefer `storage == ContainsAddress` / `ContainsData`
   - temporarily fall back to existing metadata + heuristic only for `LegacyUnclassified`
4. Annotate the known address producers/copy-address sites one family at a time
5. Once all audited sites are covered, delete the heuristic fallback and treat `LegacyUnclassified` as an internal error in reference-init code

That migration can be kept test-green throughout because behavior only flips from heuristic to explicit on audited sites.

## Weak point in the exact Option A wording

The option as stated says "every Op that produces a TempVar sets this field at construction time".

In the current IR, many op results are stored as a bare `TempVar`, not a `TypedValue` result field (`ArrayElementAddressOp`, `ComputeAddressOp`, `AssignmentOp`, etc.). So in practice the storage bit would be attached at the point where those results are wrapped into a `TypedValue` for downstream consumption.

That is workable, but it means the team should acknowledge that the true unit is "**every path that packages a result as a `TypedValue`**," not literally every op struct field.

## 5. Option B — encode the copy in a dedicated opcode (`CopyReferenceOp`)

## What it would change

Introduce something like:

```cpp
struct CopyReferenceOp {
TempVar result;
std::variant<StringHandle, TempVar> source;
Type pointee_type = Type::Invalid;
SizeInBits pointee_size_bits;
bool is_rvalue_reference = false;
};
```

This would replace the current pattern where `AssignmentOp` is used with `dereference_rhs_references = false` to mean "copy the stored pointer/reference address, do not load the pointee".

## Files / call sites likely affected

At minimum:

1. `src/IRTypes_Core.h` — add a new opcode enum value
2. `src/IRTypes_Ops.h` — define `CopyReferenceOp`
3. `src/IRTypes_Instructions.h` — printing / typed-payload support
4. `src/IRConverter_ConvertMain.h` — handler declaration
5. `src/IRConverter_ConvertMain.cpp` — new handler + dispatch + cleanup of `AssignmentOp` special-case logic
6. `src/IrGenerator_Expr_Primitives.cpp` — the two obvious `AssignmentOp(... false)` sites (`950-980`, `1129-1158`)
7. `src/IrGenerator_Expr_Conversions.cpp` — dereference-to-reference copy (`1504-1533`)
8. `src/IrGenerator_NewDeleteCast.cpp` — address-copy helper (`645-656`)

That is also roughly **8 files**, but fewer semantic producer sites than Option A.

## Impact on `GlobalTempVarMetadataStorage`

Also **no required breakage**.

The metadata store would still be needed for value category / lvalue tracking. `CopyReferenceOp` would only remove one ambiguous opcode use from the current system.

## Would Option B prevent the current bug class?

**Not by itself.**

This is the decisive point from the audit.

Option B fixes only the subset of the problem where the compiler currently says:

- "use `AssignmentOp` but secretly mean pointer copy"

That is real, and the dedicated opcode would be cleaner.

But the bugs described in the issue are not limited to that path. The current fragility also comes from:

- `ArrayElementAddressOp` temps used as `VariableDeclOp` reference initializers (`IrGenerator_Stmt_Decl.cpp:1563-1605`, `2525-2544`)
- `ComputeAddressOp` temps used the same way (`IrGenerator_Stmt_Decl.cpp:2943-2967`)
- address synthesis via generic arithmetic (`IrGenerator_Expr_Conversions.cpp:732-763`)
- other `TypedValue` packaging sites that still require `handleVariableDecl` to decide `MOV` vs `LEA`

A new `CopyReferenceOp` does **not** solve those unless the design is extended so that **reference binding itself** stops flowing through `VariableDeclOp` + ambiguous `TypedValue` initializers.

So Option B is a good cleanup, but as stated it is **too narrow** to be the primary fix.

## Migration path that can keep tests green

Yes, but only for the limited cleanup it performs:

1. Add `CopyReferenceOp` and its handler while keeping the old `AssignmentOp(... false)` behavior
2. Switch one IR-generation site at a time
3. Once all known call sites are converted, remove the `AssignmentOp` special-case path

This is test-friendly, but it does not eliminate the need for a separate answer to the `VariableDeclOp` / address-temp problem.

## 6. Recommendation

## Preferred direction: Option A first, optionally followed by Option B later

I recommend **Option A as the primary architectural fix**.

Reason:

- It directly targets the actual failure point in `handleVariableDecl`
- It scales to every current and future address-producing path, including `ArrayElementAddressOp`, `ComputeAddressOp`, and any future dedicated opcode
- It also covers address-valued temps that are packaged through helper functions rather than through a dedicated op handler
- It lets the converter stop guessing

I do **not** recommend Option B as the primary fix by itself, because it only regularizes one ambiguous encoding (`AssignmentOp(... false)`) and leaves the broader address-temp problem intact.

### Best practical recommendation

1. Implement **Option A** (preferably with `LegacyUnclassified/ContainsData/ContainsAddress` during migration)
2. Once the storage bit is authoritative and the heuristic is gone, optionally add **Option B** later as a readability/maintainability cleanup for the current pointer-copy use of `AssignmentOp`

That sequencing gives correctness first and opcode hygiene second.

## 7. Concrete implementation plan (recommended: Option A)

### Phase 1: introduce explicit storage kind

- Add `ValueStorage` to `TypedValue` in `src/IRTypes_Ops.h`
- Initialize it in every `makeTypedValue(...)` / `toTypedValue(...)` helper in `src/IROperandHelpers.h`
- Start with `LegacyUnclassified` or `ContainsData` by default; `LegacyUnclassified` is safer during migration

### Phase 2: teach reference initialization to use the explicit bit

In `src/IRConverter_ConvertMain.cpp:6221-6252`, replace the current temp-init branch with logic of the form:

- if source is known reference/address metadata -> `MOV`
- else if `init.storage == ValueStorage::ContainsAddress` -> `MOV`
- else if `init.storage == ValueStorage::ContainsData` -> `LEA`
- else -> temporary compatibility fallback / assertion during migration

The key point is that the heuristic disappears from the final state.

### Phase 3: annotate the known address producers

At minimum audit and annotate:

- `IrGenerator_Stmt_Decl.cpp:1563-1605`
- `IrGenerator_Stmt_Decl.cpp:2525-2544`
- `IrGenerator_Stmt_Decl.cpp:2943-2967`
- `IrGenerator_Expr_Conversions.cpp:640-663`
- `IrGenerator_Expr_Conversions.cpp:988-1011`
- `IrGenerator_Expr_Conversions.cpp:732-763`
- `IrGenerator_Expr_Primitives.cpp:950-980`
- `IrGenerator_Expr_Primitives.cpp:1129-1158`
- `IrGenerator_Expr_Conversions.cpp:1504-1533`
- `IrGenerator_NewDeleteCast.cpp:645-656`
- `IrGenerator_Expr_Operators.cpp:318-340`, `380-417`

### Phase 4: remove fallback guessing

- Delete `is_likely_pointer`
- Treat missing storage classification in reference binding as an internal compiler error
- Keep `TempVarMetadata` for lvalue/xvalue and aliasing information only

## 8. If the team still wants Option B later

Once Option A has removed the heuristic, `CopyReferenceOp` becomes a straightforward cleanup.

A reasonable sketch is:

```cpp
struct CopyReferenceOp {
TempVar result;
std::variant<StringHandle, TempVar> source;
Type pointee_type = Type::Invalid;
SizeInBits pointee_size_bits;
bool is_rvalue_reference = false;
};
```

A simple converter contract would be:

- load the 64-bit pointer/address from `source`
- store it to `result`
- stamp `setReferenceInfo(...)` on the result slot

The first IR-generation sites to convert would be:

1. `src/IrGenerator_Expr_Primitives.cpp:965-980`
   - reference identifier `LValueAddress` path
2. `src/IrGenerator_Expr_Primitives.cpp:1141-1158`
   - local reference-variable `LValueAddress` path
3. `src/IrGenerator_Expr_Conversions.cpp:1518-1533`
   - dereference-to-lvalue-reference materialization path

A fourth follow-up site is the helper in `src/IrGenerator_NewDeleteCast.cpp:649-656`.

Again: this is worth doing **after** the real correctness source-of-truth problem is solved.

## 9. Bottom line

- The current bug class exists because address-ness is reconstructed, not authored
- The reconstruction is split between `TempVarMetadata`, converter-side indirect-storage state, and a final `is_likely_pointer` guess
- `CopyReferenceOp` is a good cleanup, but it is too narrow to solve the full problem alone
- The smallest complete architectural fix is to make storage kind explicit in the IR data flow that reaches `handleVariableDecl`

**Recommendation: choose Option A as the primary fix, with an assertive migration (`LegacyUnclassified -> ContainsAddress/ContainsData`), and treat Option B as a later cleanup rather than the main solution.**
