# Known Issues

*All previously known issues have been fixed.*

## Same-Named Types in Different Namespaces

### Status: FIXED

The `lookupTypeInCurrentContext` function now prefers namespace-qualified lookup,
matching C++ name resolution semantics.

## Template Instantiation Namespace Tracking

### Status: FIXED

All template instantiation paths now derive the declaration-site namespace from the
template name or struct declaration name. Additionally:

- `compute_and_set_mangled_name` recovers namespace from the struct's `NamespaceHandle`
  when the current namespace is empty (template instantiation from a different namespace).
- `instantiate_full_specialization` now calls `compute_and_set_mangled_name` on
  member functions (was previously missing).
- Codegen definition, direct call, and indirect call paths all recover namespace from
  `NamespaceHandle` as a fallback when `current_namespace_stack_` is empty.
- `instantiateLazyNestedType` now derives namespace from the parent class's
  `NamespaceHandle` instead of parsing the nested type's qualified name (which
  would treat mangled class names as namespaces).
- Namespace recovery logic is consolidated into `buildNamespacePathFromHandle()`
  in `NamespaceRegistry.h`.

## Constexpr Array Dimensions from Constexpr Variable

### Status: FIXED (verified 2026-03-05)

Using a `constexpr int` variable as an array dimension now works correctly.
`constexpr int N = 42; int arr[N];` produces an array of the correct size.

## Default Function Arguments

### Status: FIXED (2026-03-05)

Calling a function with fewer arguments than declared parameters now works
when the omitted parameters have default values. Example:
`int add(int a, int b = 32); add(10);` correctly calls `add(10, 32)`.

Fix: Overload resolution (`resolve_overload` and `lookup_function`) now accounts
for trailing default parameters. Default argument expressions are filled in at
the call site during parsing.

## Compound Assignment on Global/Static Local Variables

### Status: FIXED (2026-03-05)

Compound assignment operators (`+=`, `-=`, `*=`, etc.) on global variables and
static local variables now generate proper GlobalLoad → arithmetic → GlobalStore
IR. Previously, only simple assignment (`=`) was handled for globals; compound
assignments silently lost the store.

## Range-Based For Loop with Unsized Arrays

### Status: FIXED (2026-03-05)

Range-based for loops over unsized arrays (`int arr[] = {1,2,3}`) now work.
The array size is inferred from the initializer list when not explicitly declared.

## 9–16 Byte Struct Caller/Callee ABI Mismatch

### Status: FIXED (2026-03-06)

Structs of 9–16 bytes (e.g., `struct ThreeInt { int a, b, c; }`) previously crashed
at runtime. The caller used the System V AMD64 two-register convention (values in
RDI + RSI) while the callee used the pointer convention (RDI = pointer to struct).

Fix: `isTwoRegisterStruct` now always returns `false` and `shouldPassStructByAddress`
returns `true` for any struct > 64 bits. FlashCpp uses the pointer convention for all
structs larger than one register on both Linux and Windows, consistent with the existing
callee prologue.

Note: This is not fully ABI-compatible with the System V AMD64 spec (which mandates
two-register pass for 9–16 byte structs). A TODO item exists to implement the full
two-register callee prologue for proper external ABI compatibility.

## const& Struct Default Arguments

### Status: FIXED (2026-03-06)

`const T&` struct parameters with braced-init default values (e.g.,
`void f(const Point& p = {1, 2})`) previously caused a segfault. The default
argument TypedValue was not marked as a reference, so the caller passed the struct
bytes by value instead of by pointer, and the callee dereferenced garbage.

Fix: Both the `InitializerListNode` and `ExpressionNode` default fill-in paths in
`CodeGen_Call_Direct.cpp` and `CodeGen_Call_Indirect.cpp` now set
`ref_qualifier = ReferenceQualifier::LValueReference` when the parameter type is
a reference, ensuring the caller passes the address of the temporary.

## Assignment Through Reference-Returning Methods

### Status: Open

Assigning through a reference returned by a member function (e.g.,
`h.getRef() = 42;` where `getRef()` returns `int&`) does not update the
underlying member. The returned reference is treated as an rvalue rather
than an lvalue. Workaround: assign directly to the member.

## Increment/Decrement on Static Member Variables

### Status: Open

Using `++` or `--` on static member variables accessed from within member
functions (e.g., `count++` inside `static void increment()`) does not
store the result back. The GlobalStore is generated in IR but the
IrConverter fails to find the global variable during x86 codegen because
the qualified name (`Counter::count`) doesn't match the registered global.
Compound assignment (`count += 1`) has the same issue for static members.
Workaround: use explicit assignment (`count = count + 1;`).

## Array of Structs Aggregate Initialization

### Status: Open

Initializing an array of structs with nested brace initializers
(e.g., `Pair arr[3] = {{1, 2}, {3, 4}, {5, 6}};`) fails to parse.
The parser does not handle nested initializer lists for array elements.

## System V AMD64 ABI: Two-Register Struct Passing (Partial)

### Status: Open (documented, not yet implemented)

FlashCpp uses a pointer convention for all structs > 8 bytes for both caller and
callee, which is consistent internally but deviates from the System V AMD64 ABI
for 9–16 byte structs. External C libraries or compiler-generated code that passes
such structs in two registers (per the spec) will be incompatible. Implementing the
full two-register callee prologue (unpack RDI + RSI into a local stack slot) is
needed for full ABI compatibility with external code.

