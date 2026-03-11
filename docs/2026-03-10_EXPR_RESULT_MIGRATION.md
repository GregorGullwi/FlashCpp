# ExprResult Migration Plan

**Date**: 2026-03-10
**Status**: In Progress
**Related**: TODO #22 (Pointer Type in `Type` Enum), PR #878

## Problem

Expression-evaluating codegen functions (e.g. `generateArraySubscriptIr`,
`generateIdentifierIr`, `generateMemberAccessIr`) return
`std::vector<IrOperand>` with positional semantics:

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

    // Backward-compat: implicit conversion to InlineVector<IrOperand, 4>
    operator InlineVector<IrOperand, 4>() const;
};
```

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
- `generateIdentifierIr` in `CodeGen.h`
- `makeMemberResult` in `CodeGen_MemberAccess.cpp`

### Phase 3: Change function signatures (optional, longer-term)

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

This rule is a limitation of the single-slot encoding.  `ExprResult` eliminates
the need to choose — both fields are always available.
