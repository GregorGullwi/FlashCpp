# Indirect Storage / Address-Only Cleanup Plan

## Why this note exists

The recent `AddressOfMember` bug fix exposed that backend code currently mixes two related but distinct concepts under `reference_stack_info_`:

- **true references**
  - stack slot contains an address to an object
  - consumers should usually load the pointer and then implicitly dereference/store through it
- **address-only temporaries**
  - stack slot contains a plain pointer value produced by operations like `AddressOf` / `AddressOfMember`
  - consumers should load that pointer value with `MOV`, but should **not** implicitly dereference/store through it

The current representation is workable for small fixes, but the naming and split of responsibility make it easy for new code to accidentally treat any `reference_stack_info_` entry as a true reference.

## Status Check (2026-03-13)

Parts of this plan have already landed:

- helper registration now exists via `setIndirectStorageInfo(...)`, `setReferenceInfo(...)`, and `setAddressOnlyInfo(...)`
- helper queries now exist via `getIndirectStackInfo(...)`, `hasIndirectStackStorage(...)`, `hasIndirectStorage(...)`, and `shouldImplicitlyDeref(...)`
- `AddressOf` and `AddressOfMember` already record address-only pointer results through helpers instead of open-coding side-table writes
- `reference_stack_info_` has been renamed to `indirect_stack_info_`
- `ReferenceInfo` has been renamed to `IndirectStorageInfo`
- TempVar metadata now supports `AddressOnly` via `holds_address_only` field

What has **not** landed yet:

- there is still no single shared "is this valid as a pointer base?" helper for member access / compute-address lowering (partially addressed via `isPointerBaseStorage`)

## What was intentionally kept small in the current patch

The immediate cleanup should stay narrow:

- factor address-only registration behind helpers
- stop duplicating the `AddressOf` vs `AddressOfMember` side-table setup
- prefer helper predicates over raw `reference_stack_info_.count()/find()` checks in touched code

Status today (2026-03-13):

- factor address-only registration behind helpers ✅
- stop duplicating the `AddressOf` vs `AddressOfMember` side-table setup ✅
- prefer helper predicates over raw `reference_stack_info_.count()/find()` checks in touched code ✅
- rename `reference_stack_info_` to `indirect_stack_info_` ✅
- TempVar metadata supports `AddressOnly` ✅
- `makeReference()` uses `ValueCategory` enum instead of bool ✅

### Track 0 Completed (2026-03-13)

The following files have been migrated from raw `.find()`/`.count()` lookups to semantic helpers:

- `IRConverter_Conv_Arithmetic.h` - All reference_stack_info_ lookups replaced with `getIndirectStackInfo()` and `shouldImplicitlyDeref()`
- `IRConverter_Conv_ControlFlow.h` - `.count()` replaced with `hasIndirectStackStorage()`
- `IRConverter_Conv_VarDecl.h` - Lookups replaced with `getIndirectStackInfo()` and `shouldImplicitlyDeref()`
- `IRConverter_Emit_CompareBranch.h` - Lookup replaced with `getIndirectStackInfo()`
- `IRConverter_Conv_CorePrivate.h` - Binary op handling refactored with helpers

New helper added:
- `isPointerBaseStorage(int32_t stack_offset)` - for member-access / compute-address decisions

This note captures the **larger** cleanup that should happen later.

## Main problems to solve later

### 1. `reference_stack_info_` name no longer matches reality

It is now really an **indirect storage** side table, not a pure “reference” table.

Better long-term directions:

- rename it to something like `indirect_stack_info_`
- or hide it completely behind semantic helpers and make direct access private-by-convention

### 2. TempVar metadata is asymmetric with stack metadata

Today:

- TempVar metadata can represent true references (`makeReference` / `is_address`)
- stack-offset metadata can represent both true references and address-only values (`holds_address_only`)

That means the compiler has no single source of truth for “this storage is indirect, but not a real reference.”

The cleaner long-term model is to let TempVar metadata distinguish at least:

- `None`
- `Reference`
- `AddressOnly`

so that address-carrying temps do not depend on stack-offset-only side channels.

### 3. Raw presence checks are too easy to misuse

Patterns like these are fragile:

- `reference_stack_info_.count(offset) > 0`
- `reference_stack_info_.find(offset) != end()`

They answer only “is there indirect storage metadata?” but not:

- should this be dereferenced?
- should this just be loaded as an address?
- is this valid as a pointer base for member access / compute-address?

Preferred helper layer:

- `getIndirectStackInfo(...)`
- `hasIndirectStackStorage(...)` / `hasIndirectStorage(...)`
- `shouldImplicitlyDeref(...)`
- `isPointerBaseStorage(...)`

### 4. Member access and compute-address should share more logic

`MemberAccess`, `AddressOfMember`, and `ComputeAddress` all answer a similar question:

- is the base already an address/pointer, or do we need `LEA` from a local object slot?

That decision should eventually come from one shared helper rather than repeated file-local logic.

## Suggested future implementation plan

### Track 0: COMPLETED (2026-03-13)

- All files listed above have been migrated to use semantic helpers
- `isPointerBaseStorage(...)` helper added

### Track 1: COMPLETED (2026-03-13)

- Renamed `ReferenceInfo` struct to `IndirectStorageInfo`
- Renamed `reference_stack_info_` map to `indirect_stack_info_`
- Updated all usages across the codebase

### Track 2: COMPLETED (2026-03-13)

- Added `holds_address_only` field to TempVarMetadata
- Added `makeAddressOnly()` helper to create address-only metadata
- Added `isTempVarAddressOnly()` helper function
- Updated `isTempVarReference()` to exclude address-only values
- Updated `setIndirectStorageInfo()` to sync AddressOnly metadata to TempVar
- Updated `getReferenceInfo()` to handle AddressOnly TempVars

TempVar metadata now supports all three states:
- `None` - no indirect storage
- `Reference` (via `makeReference()`) - true reference, should implicitly dereference  
- `AddressOnly` (via `makeAddressOnly()`) - plain address, should NOT implicitly dereference

### Track 3: COMPLETED (2026-03-13)

- Extended `isPointerBaseStorage` to accept optional `TempVar` parameter
- Helper now checks both stack-offset map and TempVar metadata
- Replaced 6 ad-hoc patterns with centralized helper:
  - `IRConverter_Conv_ControlFlow.h`: `handleArrayAccess`, `handleArrayStore`
  - `IRConverter_Conv_Memory.h`: `handleMemberAccess`, `handleMemberStore`, `handleAddressOfMember`, `handleComputeAddress`
- Removed redundant `"this"` string checks since 'this' is registered in `indirect_stack_info_` via `setAddressOnlyInfo`

### Track 4: Add focused regression coverage

- non-EH nested-member copy constructor path using `AddressOfMember`
- direct by-address call from `&obj.member`
- EH catch-by-value case using nested member copying
- at least one reference-catch / virtual-base case to ensure address-only values do not regress true-reference behavior

## Non-goals for the future cleanup

- do not mix this with parser/mangling/template-finalization changes
- do not redesign all lvalue/rvalue tracking at once
- do not block EH correctness fixes on a full metadata overhaul