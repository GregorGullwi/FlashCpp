# Known Issues

## EBO / `[[no_unique_address]]` — PR #1232

### Chained `[[no_unique_address]]` tail-padding reuse

`finalizeLayoutSize()` in `src/AstNodeTypes_DeclNodes.h` sets `layout_data_size`
to `total_size` (pre-alignment), which erases the distinction between data extent
and object extent after finalization. This means nested/chained NUA tail-padding
reuse does not work:

```cpp
struct Padded { short value; char tag; };          // sizeof == 4, dsize == 3
struct A { [[no_unique_address]] Padded p; char c; }; // sizeof == 4, but layout_data_size == 4 after finalize
struct B { [[no_unique_address]] A a; char c; };      // will NOT reuse A's inherited tail padding
```

Single-level NUA tail-padding reuse (the common case) works correctly.
