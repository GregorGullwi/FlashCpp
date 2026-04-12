# Known Issues

## Premature `layout_is_complete` during anonymous union processing

When a struct contains an anonymous union, the layout code in
`src/Parser_Decl_StructEnum.cpp` calls `struct_info->finalizeLayoutSize()`
on the **enclosing struct** (not the anonymous union) to commit the union's
extent into the parent layout. This sets `layout_is_complete = true`
(`src/AstNodeTypes_DeclNodes.h:385`) while the enclosing struct still has
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
