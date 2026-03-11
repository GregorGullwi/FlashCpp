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

## What was intentionally kept small in the current patch

The immediate cleanup should stay narrow:

- factor address-only registration behind helpers
- stop duplicating the `AddressOf` vs `AddressOfMember` side-table setup
- prefer helper predicates over raw `reference_stack_info_.count()/find()` checks in touched code

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

- `getIndirectStorageInfo(...)`
- `hasIndirectStorage(...)`
- `shouldImplicitlyDeref(...)`
- `isPointerBaseStorage(...)`

### 4. Member access and compute-address should share more logic

`MemberAccess`, `AddressOfMember`, and `ComputeAddress` all answer a similar question:

- is the base already an address/pointer, or do we need `LEA` from a local object slot?

That decision should eventually come from one shared helper rather than repeated file-local logic.

## Suggested future implementation plan

### Track 1: Rename the concept

- introduce an `IndirectStorageInfo` type name (or alias)
- rename `reference_stack_info_` after the broad EH work settles

### Track 2: Extend TempVar metadata

- add an explicit address-kind enum or equivalent
- teach `AddressOf`, `AddressOfMember`, and similar pointer-producing ops to persist `AddressOnly` metadata on the temp itself

### Track 3: Migrate backend consumers

- replace raw map lookups in memory/member/call lowering with semantic helpers
- keep all implicit-deref decisions centralized

### Track 4: Add focused regression coverage

- non-EH nested-member copy constructor path using `AddressOfMember`
- direct by-address call from `&obj.member`
- EH catch-by-value case using nested member copying
- at least one reference-catch / virtual-base case to ensure address-only values do not regress true-reference behavior

## Non-goals for the future cleanup

- do not mix this with parser/mangling/template-finalization changes
- do not redesign all lvalue/rvalue tracking at once
- do not block EH correctness fixes on a full metadata overhaul