# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Indirect calls through function pointers returning struct types

`emitIndirectCall` (`src/IrGenerator_Call_Indirect.cpp`) computes the return
size via `get_type_size_bits(ret_type)` and wraps it with `nativeTypeIndex(ret_type)`.
Both helpers only handle native/primitive `TypeCategory` values correctly —
`get_type_size_bits` returns `0` for `TypeCategory::Struct` (falls through to
the `default` case in `src/AstNodeTypes.cpp:399-447`), and `nativeTypeIndex`
returns a placeholder `TypeIndex{0, Struct}` instead of the real struct's type
index.

This means that if a function pointer's return type is a struct, the
`ExprResult` produced by `emitIndirectCall` will carry a zero size and an
invalid type index, which may cause incorrect code generation downstream.

The same limitation exists in all pre-existing indirect-call sites that were
consolidated into `emitIndirectCall` (e.g. the `MemberAccessNode` function
pointer paths). It is **not** a regression introduced by PR #1094.

**Affected code:**
- `AstToIr::emitIndirectCall` — `src/IrGenerator_Call_Indirect.cpp:1790-1792`
- `get_type_size_bits` — `src/AstNodeTypes.cpp:399-447`
- `nativeTypeIndex` — `src/AstNodeTypes.cpp:164-170`

**Workaround:** None currently. Function pointers whose return type is a struct
are not yet exercised by the test suite.
