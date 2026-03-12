# ExprResult Migration Plan

**Date**: 2026-03-10
**Status**: In Progress (Phase 4 consumer-side `toExprResult` removal underway)
**Related**: TODO #22 (Pointer Type in `Type` Enum), PR #878

## Status Check (2026-03-12)

- `ExprResult` and `ExprOperands` are present in `src/IROperandHelpers.h`
- `toTypedValue(const ExprResult&)` already exists, so consumer-side bridging is started
- priority producer sites have been migrated in `src/CodeGen_MemberAccess.cpp`, `src/CodeGen_Expr_Primitives.cpp`, and selected address-expression paths in `src/CodeGen_Expr_Conversions.cpp`
- the current implementation still uses `encoded_metadata` as a temporary compatibility escape hatch for legacy slot-4 enum encoding edge cases
- the remaining compatibility work is no longer “add ExprResult” — it is “finish removing the positional bridge and slot-4 compatibility layer” after the Phase 3 return-signature migration

### Branch progress (PR #893 / 2026-03-12)

- all planned Phase 3 producer families on this branch now return `ExprResult` directly:
  - leaf literals
  - leaf identifiers
  - member/subscript
  - casts
  - unary/binary/ternary operators
  - calls
  - remaining intrinsic/helper producers
  - `visitExpressionNode`
- positional caller updates were completed for the migrated producer families and
  their directly touched helper paths, replacing many `result[0]` / `result[2]` /
  `result[3]` reads with named `ExprResult` fields
- follow-up validation on this branch exposed a non-migration runtime mismatch in
  `tests/template_parsing_test_ret0.cpp`; the local direct-call lowering now inserts
  standard primitive argument conversions before calls, and cross-domain literal
  conversions are routed through conversion IR so the backend receives a typed temporary
  instead of a mismatched immediate payload
- `encoded_metadata`, `operator ExprOperands()`, and `toExprResult(...)` remain
  intentionally in place for the later cleanup phase
- some compatibility consumers still immediately materialize `ExprOperands` from
  `ExprResult` return values or bounce through `toExprResult(...)` at callsites;
  these remaining adapters are now the main Phase 4 cleanup target
- additional Phase 4 cleanup on this branch migrated unary-expression handling in
  `src/CodeGen_Expr_Conversions.cpp`: the local `tryBuildIdentifierOperand`
  builder and `operandIrOperands` now use `ExprResult`, the last 4
  `toExprResult(...)` bounce points in that file were removed, and the nearby
  unary `+`, `++`, `--`, `&`, and `*` paths now consume named fields instead of
  positional operand indexes
- a further Phase 4 follow-up reduced the remaining
  `src/CodeGen_Expr_Operators.cpp` bounce points by threading `ExprResult`
  alongside legacy `ExprOperands` in the compound-assignment flow:
  - both compound-assignment metadata-handler paths now call
    `handleLValueCompoundAssignment(...)` with the original `ExprResult`
    returned from `visitExpressionNode()`
  - the `op=` typed IR block now returns `lhsExprResult` directly and uses
    `.value` for the result target instead of re-decoding `lhsIrOperands`
  - the nearby assignment/common-type conversion sites now keep
    `lhsExprResult` / `rhsExprResult` synchronized when
    `generateTypeConversion(...)` materializes converted temporaries
- after those follow-ups, the remaining known consumer-side `toExprResult(...)`
  bounce points on this branch are still concentrated in
  `src/CodeGen_Expr_Operators.cpp`, but mostly in the older assignment/helper
  paths rather than the typed compound-assignment block
- another follow-up then trimmed most of those older assignment/helper adapters:
  - the array/member assignment fast path now carries `lhsExprResult` /
    `rhsExprResult` through `handleLValueAssignment(...)` directly and returns
    `rhsExprResult`
  - the implicit-member and captured-by-reference assignment paths now call
    `handleLValueAssignment(...)` with direct `ExprResult` locals instead of
    immediately bouncing through `toExprResult(...)`
  - the general unified lvalue-assignment path now returns `rhsExprResult`
    directly, and the global/static assignment helper keeps only a local
    `ExprOperands` bridge for its positional store logic before returning the
    original `ExprResult`
- after that slice, the remaining known consumer-side `toExprResult(...)`
  bounce points in `src/CodeGen_Expr_Operators.cpp` were down to the comma
  operator, the `va_list` helpers, and the builtin launder return path
- the next follow-up removed those last `CodeGen_Expr_Operators.cpp`
  consumer-side bounce points too:
  - the comma operator now returns the direct `ExprResult` from its RHS
  - `generateVaArgIntrinsic(...)` and `generateVaStartIntrinsic(...)` now pass
    the original `ExprResult` into `isVaListPointerType(...)` and use `.value`
    instead of positional slot access for the tracked va_list variable
  - `generateBuiltinLaunderIntrinsic(...)` now uses named `ExprResult` fields
    and returns the original pointer result directly
- after that cleanup, the known Phase 4 consumer-side `toExprResult(...)`
  bounce points are no longer in `src/CodeGen_Expr_Operators.cpp`

## Problem

Expression-evaluating codegen functions (e.g. `generateArraySubscriptIr`,
`generateIdentifierIr`, `generateMemberAccessIr`) historically returned
`std::vector<IrOperand>` with positional semantics. The fixed-size hot path has
now moved to `ExprOperands` (`InlineVector<IrOperand, 4>`), but much of the
legacy positional contract still exists at the producer/consumer boundary:

```
[0] Type               — element type
[1] int                — size_in_bits
[2] IrOperand (value)  — TempVar / StringHandle / literal
[3] unsigned long long  — ??? (type_index OR pointer_depth)
```

The 4th slot is a raw `unsigned long long` whose meaning depends on an
implicit contract between producer and consumer:

- **Producers** (e.g. `generateArraySubscriptIr`) decide what to put there
  based on `element_type == Type::Struct`.
- **Consumers** (`toTypedValue` in `IROperandHelpers.h`) decide how to
  interpret it based on `type == Type::Struct || Type::Enum || Type::UserDefined`.

These conditions can silently diverge.  PR #878 introduced exactly this bug:
the consumer included `Type::Enum` in the type_index branch, but the producer
in array subscript codegen only checked `Type::Struct`, so enum pointer arrays
got their `pointer_depth` misinterpreted as `type_index`.

Note: a regression test (`test_enum_pointer_array_subscript_ret0.cpp`) was
added but does not currently fail — the bug requires `pointer_depth` to flow
through `toTypedValue` at a site where the backend actually uses it, which
the current test's code path doesn't exercise deeply enough.  The test exists
as a canary for future changes that might make the path reachable.

## Root Cause

The `vector<IrOperand>` return type is a hand-rolled positional tuple with no
type safety.  Any code that constructs `{ type, size, value, some_ull }` can
silently put the wrong semantic value in the 4th slot, and the compiler won't
catch it.

Additionally, `std::vector` heap-allocates for every expression result, even
though these are always exactly 3 or 4 elements.

## Solution: `ExprResult` Struct with `InlineVector` Storage

Replace the raw `std::vector<IrOperand>` with a struct that has named fields
and uses `InlineVector<IrOperand, 4>` for the backward-compatible conversion.
Since expression results are always 3-4 elements, `InlineVector<IrOperand, 4>`
keeps everything on the stack — no heap allocation ever.

```cpp
struct ExprResult {
    Type type;
    int size_in_bits;
    IrOperand value;          // TempVar, StringHandle, literal
    TypeIndex type_index = 0;
    int pointer_depth = 0;

    // Transitional compatibility override for legacy slot-4 encoding.
    std::optional<unsigned long long> encoded_metadata;

    // Backward-compat: implicit conversion to ExprOperands.
    operator ExprOperands() const;
};
```

`encoded_metadata` is a temporary migration aid, not the end state. It exists
to preserve legacy enum-specific slot-4 behavior while remaining producers and
consumers are still being migrated away from positional metadata.

### Why this works

1. **Producers can't get the encoding wrong.** They set `type_index` and
   `pointer_depth` as separate named fields.  There's no discrimination logic
   to implement — you just assign both.

2. **Encoding is centralized.** The `operator InlineVector<IrOperand, 4>()`
   conversion handles the 4th-operand encoding in exactly one place, matching
   `toTypedValue()`'s decode.

3. **Incremental migration.** Since `ExprResult` implicitly converts to
   `InlineVector<IrOperand, 4>`, existing callers that expect a vector keep
   compiling.  You can migrate one function at a time.

4. **Compile-time safety.** Once a producer uses `ExprResult`, any attempt to
   construct one with `{ type, size, var, raw_ull }` will fail — you're forced
   to use the named fields.

5. **Zero heap allocations.** `InlineVector<IrOperand, 4>` stores all 4
   elements inline.  Expression results never exceed 4 elements, so the
   overflow path is never hit.

### Chained / recursive expressions

Binary operations (`a + b * c`) don't use the 4th operand at all.  They:
- Consume operands via `toTypedValue()`, which handles both 3 and 4-element vectors
- Produce 3-element results `{type, size, result_var}` — no metadata

The `type_index`/`pointer_depth` metadata only matters at **leaf expressions**
(identifiers, array subscripts, member access) and at **consumption boundaries**
(call sites, assignments).  `ExprResult` is only needed at those leaf sites.

## Migration Steps

### Phase 1: Implement `InlineVector` and add `ExprResult`

Implement `InlineVector<T, N>` (described in `TYPE_LOOKUP_OPTIMIZATION_PLAN.md`)
as a real class in a header (e.g. `src/InlineVector.h`).  Add `ExprResult` to
`IROperandHelpers.h`.  Change the return type of expression-evaluating functions
from `std::vector<IrOperand>` to `InlineVector<IrOperand, 4>`.

All existing code continues to work — nothing returns `ExprResult` yet, but
the heap allocation overhead is eliminated.

**PR #882 status (2026-03-11):**
- `ExprResult` was added in `IROperandHelpers.h`
- the existing `InlineVector` was reused for `ExprOperands`
- fixed-size expression-result return types were migrated from
  `std::vector<IrOperand>` to `InlineVector<IrOperand, 4>`
- hot-path consumers such as `toTypedValue(const ExprOperands&)` and
  `generateTypeConversion(...)` must stay on `ExprOperands` (or another
  non-owning/fixed-size view) to avoid implicitly converting back to
  `std::vector<IrOperand>` and reintroducing heap allocation
- variable-length operand builders used to assemble call packets remain
  `std::vector<IrOperand>` internally; they are not the fixed 3-4 operand
  expression-result tuple that this migration is targeting

**Phase 1 clarification discovered during PR #882:**
- `InlineVector::data()` must not be used to form a span over an `InlineVector`
  that may overflow, because its storage is not guaranteed to be contiguous
  across inline and overflow elements
- fixed-size expression-result consumers should index/copy the 3-4 operands
  directly instead of assuming contiguous backing storage

**Original `std::vector<IrOperand>` parameter sites deferred to Phase 2 (44960e4):**
These functions still accepted `const std::vector<IrOperand>&`, causing implicit
`InlineVector→std::vector` heap-allocating conversions when called with
`ExprOperands`. They were deferred because Phase 2 would change them to accept
`const ExprResult&` with named fields, making a Phase 1 `const ExprOperands&`
intermediate step wasteful:
- `handleLValueAssignment` (`AstToIr.h:458`) — 2 params
- `handleLValueCompoundAssignment` (`AstToIr.h:465`) — 2 params
- `extractBaseFromOperands` (`AstToIr.h:201`)
- `extractBaseOperand` (`AstToIr.h:227`)
- `markReferenceMetadata` (`AstToIr.h:231`)
- `isVaListPointerType` (`AstToIr.h:178`)
- `handleRValueReferenceCast` (`AstToIr.h:245`)
- `handleLValueReferenceCast` (`AstToIr.h:251`)
- `generateUnaryIncDecOverloadCall` (`AstToIr.h:284`)
- `generateBuiltinIncDec` (`AstToIr.h:296`)

These were not a regression: before this PR they received `std::vector`
directly (no conversion). The implicit conversion cost is equivalent to the
previous heap allocation at the producer site. Phase 2 will eliminate both
by switching to `ExprResult`.

**Current remaining legacy positional helper sites (after 2026-03-12 follow-up):**
- none in the original Phase 2 deferred helper list

All helpers from the original deferred list now take `const ExprResult&` and
consume named fields directly. The remaining compatibility concerns are now
isolated to producer return signatures and the temporary slot-4 bridge.

### Phase 2: Migrate producers one at a time

For each function that constructs a 4-element operand vector with
type_index/pointer_depth metadata, change the `return` statements to use
`ExprResult`:

```cpp
// Before:
unsigned long long fourth = (element_type == Type::Struct)
    ? static_cast<unsigned long long>(element_type_index)
    : ((element_pointer_depth > 0) ? ... : 0ULL);
return { element_type, element_size_bits, result_var, fourth };

// After:
ExprResult result;
result.type = element_type;
result.size_in_bits = element_size_bits;
result.value = IrOperand{result_var};
result.type_index = static_cast<TypeIndex>(element_type_index);
result.pointer_depth = element_pointer_depth;
return result;
```

The declared return type stays `InlineVector<IrOperand, 4>`, so `ExprResult`
is converted at the return site.  No callers need to change.

**Priority producers** (these construct the 4th operand today):
- `generateArraySubscriptIr` in `CodeGen_MemberAccess.cpp`
- `generateIdentifierIr` in `CodeGen_Expr_Primitives.cpp`
- `makeMemberResult` in `CodeGen_MemberAccess.cpp`

**Phase 2 progress (2026-03-11):**
- migrated `generateArraySubscriptIr` return sites in `src/CodeGen_MemberAccess.cpp`
  to build `ExprResult` and let the conversion operator encode
  `type_index`/`pointer_depth`
- migrated `makeMemberResult` in `src/CodeGen_MemberAccess.cpp` to populate
  named `ExprResult` fields instead of manually constructing a 4-slot result
- migrated the priority 4-slot return sites in `generateIdentifierIr`
  (`src/CodeGen_Expr_Primitives.cpp`) to a tiny local `ExprResult` helper
- migrated related array-address producer returns in
  `src/CodeGen_Expr_Conversions.cpp` (`analyzeAddressExpression` result return
  and multidimensional `&arr[i][j]`)
- added `toExprResult(...)` bridge helpers in `src/IROperandHelpers.h` that
  preserve raw legacy slot-4 metadata in `ExprResult::encoded_metadata` while
  exposing named `type` / `size_in_bits` / `value` / decoded metadata fields
- migrated the deferred helper consumers in `src/CodeGen_MemberAccess.cpp`,
  `src/CodeGen_NewDeleteCast.cpp`, `src/CodeGen_Expr_Operators.cpp`, and
  `src/CodeGen_Expr_Conversions.cpp` from `const std::vector<IrOperand>&` to
  `const ExprResult&` for:
  - `extractBaseFromOperands`
  - `extractBaseOperand`
  - `markReferenceMetadata`
  - `isVaListPointerType`
  - `handleRValueReferenceCast`
  - `handleLValueReferenceCast`
  - `generateUnaryIncDecOverloadCall`
  - `generateBuiltinIncDec`
- migrated `handleLValueAssignment` and `handleLValueCompoundAssignment` to
  `const ExprResult&` and updated their call sites to consume named fields
- removed `preserveLegacyEnumPointerDepthEncoding(...)`; enum/user-defined
  pointer metadata now round-trips through `ExprResult::operator ExprOperands()`
  and `toTypedValue(const ExprResult&)` without an extra helper
- intentionally deferred broader non-priority/manual 4-slot sites outside these
  producer paths (for example other unary/conversion helpers and qualified
  identifier paths) to keep Phase 2 reviewable and producer-focused

## Immediate Implementation Checklist

The plan is easier to execute if it is split into these concrete slices:

1. **Finish producer cleanup before changing public signatures**
   - migrate any remaining manual 4-slot expression-result builders
   - prefer tiny local `ExprResult` builders over open-coded `{ type, size, value, metadata }`
2. **Convert legacy expression-result helper parameters** ✅ complete
   - all helpers from the original deferred `AstToIr.h` list now take
     `const ExprResult&`
3. **Change return signatures now that helper parameters are migrated** ✅ branch-complete for the planned producer families
    - Phase 3 is now unblocked because the last legacy positional helper
      parameters have been removed
4. **Delete the compatibility shim last**
    - remove `encoded_metadata`
    - remove slot-4 decoding from `toTypedValue()`
    - remove implicit `ExprResult` -> `ExprOperands` compatibility usage at
      remaining consumers/callsites
    - remove `toExprResult(...)` bounce points once callers consume named fields directly

### Phase 3: Change function signatures

Once all producers for a function use `ExprResult`, change its return type:

```cpp
// Before:
InlineVector<IrOperand, 4> generateArraySubscriptIr(...);

// After:
ExprResult generateArraySubscriptIr(...);
```

Callers that use `toTypedValue()` continue to work (add a
`toTypedValue(const ExprResult&)` overload).  Callers that index by position
(`result[0]`, `result[2]`) must be updated to use the named fields — this is
intentional, as those are the fragile access patterns we want to eliminate.

**2026-03-12 branch update:** the planned producer-family migration has been
completed incrementally on this branch. The remaining work after merge is the
Phase 4 compatibility cleanup: deleting the encoding bridge and converting the
remaining compatibility consumers that still materialize positional operands.

### Phase 4: Remove the encoding/decoding layer

Once all expression results flow through `ExprResult`, the
`operator InlineVector<IrOperand, 4>()` conversion and the 4th-operand
decoding in `toTypedValue()` become dead code and can be removed.

## Encoding Rule (Reference)

The 4th operand discrimination rule, used in both `ExprResult`'s conversion
operator and `toTypedValue()`:

| Type                                    | 4th operand carries |
|-----------------------------------------|---------------------|
| `Type::Struct`, `Type::Enum`, `Type::UserDefined` | `type_index`        |
| Everything else                         | `pointer_depth`     |

This rule is a limitation of the single-slot encoding. `ExprResult` eliminates
the need to choose — both fields are always available. The temporary
`encoded_metadata` override exists only so the migration can proceed
incrementally without reintroducing enum slot-4 regressions while mixed old/new
paths still coexist.

### Deferred Follow-up: Harden `makeExprResult`

**Problem:** The original `makeExprResult` has positional parameters with defaults:

```cpp
inline ExprResult makeExprResult(
    Type type,
    int size_in_bits,
    IrOperand value,
    TypeIndex type_index = 0,           // default
    int pointer_depth = 0,              // default
    std::optional<unsigned long long> encoded_metadata = std::nullopt  // default
);
```

This caused bugs like:
```cpp
// BUG: 6th arg goes to encoded_metadata, NOT type_index!
// type_index stays at default (0), causing struct resolution failures
makeExprResult(Type::Struct, size, value, 0, 0, static_cast<unsigned long long>(type_index));
```

**Narrow solution:** Replace the current defaulted helper with explicit
overloads or similarly explicit helper entry points:

```cpp
// 3 args: just type + value (most common)
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value);

// 4 args: with TypeIndex
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index);

// 4 args: with pointer_depth
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, int pointer_depth);

// 5 args: both type_index and pointer_depth
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index, int pointer_depth);

// 6 args: legacy encoded_metadata bridge (rare - only for partial migrations)
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, unsigned long long encoded_metadata);
```

This is a good **follow-up hardening step**, but it is not a prerequisite for
finishing the main `ExprResult` migration.

**Benefits:**
- The bug case `makeExprResult(..., 0, 0, ull)` no longer compiles
- Callers with `TypeIndex` variables work without changes (they're already the correct type)
- Only literal `0` for pointer_depth needs explicit cast

**Migration:**
- ~150 call sites to update
- Bug cases `makeExprResult(..., 0, 0, ull)` → `makeExprResult(..., type_index)`
- 5-arg calls `makeExprResult(..., ti, 0)` → `makeExprResult(..., ti)`

## Explicitly Out of Scope for This Plan

The following are reasonable ideas, but they should be treated as a **separate
design effort**, not as later phases of this migration plan:

- converting `TypeIndex` from an alias into a strong wrapper type
- introducing `SizeInBits` / `SizeInBytes` / `PointerDepth` wrapper types
- adding user-defined literals such as `_bits`, `_bytes`, `_ptr`, or `_type`
- doing a repo-wide API relabeling pass around those new wrapper types

Those changes are much broader than the `ExprResult` migration itself and would
touch parser/codegen/IR APIs well beyond the slot-4 compatibility cleanup. They
are now tracked separately in `docs\2026-03-12_IR_METADATA_STRONG_TYPES_PLAN.md`.
