# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Nested namespace-qualified template members can crash at runtime

Code that instantiates a class template in one namespace and stores a value in a
subobject whose type is a relative namespace-qualified class template (for
example `detail::Holder<T>` inside `outer::Box<T>`) can currently compile and
link successfully but crash at runtime with signal 11.

Current repro: `tests/test_relative_namespace_template_instantiation_ret6.cpp`

The recent parser/template-lookup fix correctly resolves the relative
namespace-qualified template name during instantiation, but the generated code
for the resulting object still appears to mishandle the instantiated subobject
at runtime.

## Range-for with inline struct iterator member functions

Range-for loops using struct iterators with inline member function definitions
(operator*, operator++, operator!=) crash at runtime (signal 11). Out-of-line
definitions work correctly. See `tests/test_range_for_auto_struct_iterator_ret0.cpp`
for a working pattern.

## Member calls on reference-valued complex receivers remain incomplete

Template or lazy member calls on reference-valued complex receivers such as
`static_cast<Foo&>(x).method()` or `(cond ? a : b).method()` can still fall
back into ordinary direct-call lowering or mis-handle the receiver object.

One current failure mode is:

```text
Phase 1: sema-normalized direct call missing resolved target for '...'
```

`parse_member_postfix` now deduces object types for arbitrary expressions, but
`generateMemberFunctionCallIr` (`src/IrGenerator_Call_Indirect.cpp`) still has
receiver-shape-specific lowering and does not yet handle all reference-valued
complex receivers all the way through member-call codegen.

**Workaround:** Bind the expression result to a named reference first:

```cpp
auto& obj = static_cast<Foo&>(x);
obj.method();
```

## Return statement implicit constructor materialization uses triple-fallback strategy

`visitReturnStatementNode` (`src/IrGenerator_Visitors_Namespace.cpp:266-329`)
materializes implicit converting constructors when a return expression's type
does not match the function's struct return type (e.g., `return 42;` when the
return type has a converting constructor from `int`). This is correct per
C++ copy-initialization semantics, but the implementation uses three
progressively looser resolution strategies:

1. **Type-based overload resolution** via `resolve_constructor_overload` with
   the argument type inferred by `buildCodegenOverloadResolutionArgType`.
2. **Arity-based resolution** via `resolve_constructor_overload_arity` matching
   single-argument constructors.
3. **Lone-viable-constructor fallback** that manually scans all non-copy/move
   constructors accepting one argument and picks the result only when exactly
   one candidate is found.

Copy and move constructors are correctly excluded via
`isImplicitCopyOrMoveConstructorCandidate`.

**Potential issue:** The triple-fallback could mask ambiguous constructor
overloads that a conforming compiler should reject. If strategy (1) fails
due to missing type information (e.g., a dependent expression whose type
`buildCodegenOverloadResolutionArgType` cannot infer), strategy (2) or (3)
may silently select a constructor that full overload resolution would have
deemed ambiguous. In practice this is unlikely to cause incorrect code for
well-formed programs, but ill-formed programs may be accepted instead of
diagnosed.

**Affected code:**
- `AstToIr::visitReturnStatementNode` — `src/IrGenerator_Visitors_Namespace.cpp:266-329`
- `resolve_constructor_overload` — type-based constructor overload resolution
- `resolve_constructor_overload_arity` — arity-only constructor overload resolution

**Possible fix:** Unify the three strategies into a single overload-resolution
call that always has access to the argument type. When the argument type
cannot be inferred, report an ambiguity diagnostic instead of falling back
to looser matching.

## Indirect calls through function pointers returning float/double store RAX instead of XMM0

`populateIndirectCallReturnInfo` is only called when `needs_type_index(sig.returnType())`
is true, which excludes primitive types (`Float`, `Double`, `Int`, etc.).
For those return types the `IndirectCallOp` arrives at codegen with default
fields: `return_type_index` has category `Invalid`, `return_size_in_bits` is 0,
and `use_return_slot` is false.

In `handleIndirectCall` (`src/IRConverter_ConvertMain.cpp`), the return-value
storage path checks `isFloatingPointType(op.returnType())` to decide whether
to store from XMM0 (float/double) or RAX (integer). Because `op.returnType()`
resolves to `TypeCategory::Invalid` for primitive-returning indirect calls,
the float branch is never taken. The return value is unconditionally stored
from RAX, which is incorrect for float and double returns per both the Win64
and SysV ABIs — the callee places float/double results in XMM0.

This is a **pre-existing bug** that was not introduced by PR #1108; the old
code also unconditionally stored RAX. The new codegen framework has the
correct branching logic in place but the IR layer does not populate the
metadata for non-aggregate return types.

**Affected code:**
- `AstToIr::populateIndirectCallReturnInfo` — `src/IrGenerator_Call_Direct.cpp`
  (guarded by `needs_type_index`, skips primitive types)
- All `IndirectCall` emission sites in `src/IrGenerator_Call_Indirect.cpp` and
  `src/IrGenerator_Call_Direct.cpp` (same guard)
- `handleIndirectCall` return-value storage — `src/IRConverter_ConvertMain.cpp`
  (checks `op.returnType()` which is `Invalid` for primitives)

**Possible fix:** Call `populateIndirectCallReturnInfo` (or an equivalent)
for **all** return types, not just those where `needs_type_index` is true.
This would populate `return_type_index` and `return_size_in_bits` for
float/double/int returns so the codegen can correctly dispatch to XMM0 vs
RAX storage. Alternatively, unconditionally set `return_type_index` to
`nativeTypeIndex(sig.returnType()).withCategory(sig.returnType())` and
`return_size_in_bits` to `get_type_size_bits(sig.returnType())` at all
indirect-call emission sites.

## populateIndirectCallReturnInfo hardcodes pointer_depth=0 and is_reference=false

`populateIndirectCallReturnInfo` (`src/IrGenerator_Call_Direct.cpp:102-111`)
passes `0` for `pointer_depth` and `false` for `is_reference` to
`needsHiddenReturnParam`. A `FunctionSignature` does not carry
reference/pointer qualifiers for its return type — those live in the
enclosing `TypeSpecifierNode`.

If a function pointer returns a struct by reference (e.g.,
`Foo& (*fp)(int)`), the signature's `returnType()` would be
`TypeCategory::Struct` while the actual return is a reference
(pointer-sized, returned in RAX). With `is_reference=false`,
`returnsStructByValue` would return true, and if the struct exceeds the
ABI threshold, `needsHiddenReturnParam` would incorrectly return true.
This would cause the codegen to emit a hidden return slot for what should
be a simple pointer return in RAX.

Compare with the direct-call path at `src/IrGenerator_Call_Direct.cpp:1876-1877`
which correctly passes `return_type.pointer_depth()` and
`return_type.is_reference()` from the full `TypeSpecifierNode`.

**Likelihood:** Low in practice. Function-pointer-to-reference-returning-function
is rare, and the signature often encodes such returns differently. However, the
mismatch between the indirect and direct call paths is a latent correctness issue.

**Affected code:**
- `AstToIr::populateIndirectCallReturnInfo` — `src/IrGenerator_Call_Direct.cpp:105-110`
- `needsHiddenReturnParam` / `returnsStructByValue` — called with hardcoded
  `pointer_depth=0`, `is_reference=false`

**Possible fix:** Extend `FunctionSignature` to carry return-type reference
and pointer qualifiers, or resolve them from the `TypeSpecifierNode` at the
call site before invoking `populateIndirectCallReturnInfo`. Alternatively,
check the resolved alias chain for reference qualifiers inside
`populateIndirectCallReturnInfo` itself.
