# Known Issues

## Premature `layout_is_complete` during anonymous union processing

When a struct contains an anonymous union, the layout code in
`src/Parser_Decl_StructEnum.cpp` calls `struct_info->finalizeLayoutSize()`
on the **enclosing struct** (not the anonymous union) to commit the union's
extent into the parent layout. This sets `layout_is_complete = true`
(`src/AstNodeTypes_DeclNodes.h:381`) while the enclosing struct still has
members left to process.

If `sizeInBytes()` were called on the enclosing struct during subsequent
member processing it would prematurely apply `enforceMinimumCompleteObjectSize`
(`src/AstNodeTypes_DeclNodes.h:339-341`), potentially returning an incorrect
size for an in-progress layout.

This does not cause issues in practice because `sizeInBytes()` is never called
on the struct being built during its own layout phase, but the invariant is
fragile. A future refactor could split `layout_is_complete` into separate
"layout computed" and "complete object" flags, or avoid calling
`finalizeLayoutSize` on the enclosing struct mid-layout.

## Dependent `sizeof`/`alignof` non-type template arguments can lose size information

While investigating PR #1242 review feedback, a targeted regression using
`SizeValue<sizeof(T)>` inside a class template compiled but produced the wrong
runtime result, and `FlashCpp -v` logged:

> `sizeof returned 0, this indicates a bug in type size tracking`

The warning is emitted in `src/IrGenerator_MemberAccess.cpp:2091` when codegen
lowers `sizeof` and the tracked byte size is still zero. A current reproducer is
equivalent to:

```cpp
template <unsigned long long N>
struct SizeValue { using type = char[(int)N]; };

template <typename T>
struct Holder {
	using size_type = SizeValue<sizeof(T)>;
	static int get() { return (int)sizeof(typename size_type::type); }
};
```

`Holder<int>::get()` currently behaves as if `sizeof(T)` were zero during the
dependent instantiation path.

This appears adjacent to the Phase 1 template-argument identity work, but the
observed failure is deeper in dependent type-size propagation rather than in the
new hash/equality cleanup itself.
