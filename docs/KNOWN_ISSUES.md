# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.


## EBO / `[[no_unique_address]]` — PR #1184

### Investigations

#### `[[no_unique_address]]` only handles empty types, not tail-padding reuse

`src/AstNodeTypes_DeclNodes.h:185-194` — The current implementation only applies
the zero-size optimization for `isEmptyLayoutLike()` types. Per C++20
\[dcl.attr.nouniqueaddr\], the attribute also permits reusing tail padding of
non-empty members. This is a standards-compliant simplification (the attribute is
permissive, not mandatory) but means `struct S { [[no_unique_address]] NonEmpty a; char c; }`
will not benefit from potential tail padding optimization.

#### `total_size` tracking for NUA empty members at non-zero offsets

`src/AstNodeTypes_DeclNodes.h:216-220` — When a NUA empty member is placed at a
non-zero offset due to same-type overlap, `total_size = offset + layout_member_size`
(with `layout_member_size == 0`) sets `total_size` to the bumped offset. This may
inflate `dsize` beyond what the Itanium ABI specifies. Needs verification against
the ABI specification for the distinction between `dsize` and `sizeof` semantics.
