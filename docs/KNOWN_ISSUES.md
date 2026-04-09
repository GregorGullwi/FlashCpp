# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.


## EBO / `[[no_unique_address]]` — PR #1184

### Bugs

#### Incomplete same-type overlap check in `addMember` (member placement)

`src/AstNodeTypes_DeclNodes.h:188` — `hasEmptySubobjectTypeAtOffset(type_index, offset)`
checks only the member's own `type_index`, not its transitive leading empty
subobjects. When an empty member inherits from the same empty base that is already
placed at the candidate offset, the conflict is missed and two subobjects of the
same type share an address.

Example that produces wrong layout:

```cpp
struct E {};
struct A : E {};
struct B : E { [[no_unique_address]] A a; };
// B::a contains an E at offset 0, which overlaps with B's own base E at offset 0
```

The base-placement path (`decideBasePlacement` in `src/AstNodeTypes.cpp:1147`)
correctly uses `haveCommonLeadingEmptyType` for the full transitive check; the
member path should do the same.

#### `is_no_unique_address` lost in several template instantiation paths

The `StructMemberDecl` constructor at `src/AstNodeTypes_Template.h:649` uses a
default parameter `bool no_unique_address = false`, which violates the project's
no-default-parameter rule (AGENTS.md) and silently drops the flag when callers
use the old overload. Affected call sites that have `member_decl.is_no_unique_address`
available but do not forward it:

- `src/Parser_Templates_Inst_ClassTemplate.cpp:4847` — primary template instantiation
- `src/Parser_Templates_Inst_ClassTemplate.cpp:5646` — nested struct instantiation
- `src/Parser_Templates_Inst_ClassTemplate.cpp:2983` — pattern substitution
- `src/Parser_Templates_Inst_Substitution.cpp:1437` — substitution path

Any `[[no_unique_address]]` member in a template class instantiated through these
paths will lose its attribute and get incorrect layout.

### Investigations

#### Base placement overlap check does not re-verify after bumping offset

`src/AstNodeTypes.cpp:1167-1168` — When `decideBasePlacement` detects an overlap
conflict, it bumps `candidate_offset` by alignment but does not re-check whether
other already-placed bases at the *new* offset also conflict. In practice this
requires a pathological inheritance pattern with 3+ empty bases sharing the same
leading type, so existing tests pass. A loop that re-checks until no conflict
exists would be more robust.

#### `last_skipped_no_unique_address_attribute_` is a fragile global flag

`src/Parser.h:472` — The parser-level flag is set in three different places
(`skip_cpp_attributes`, `parse_member_leading_specifiers`, and
`starts_with_no_unique_address_attribute`) and reset only at the start of each
member parse iteration. Any future call to `skip_cpp_attributes` in an unexpected
context could leave the flag set for the wrong member. A more robust approach
would be to have `skip_cpp_attributes` return a struct of detected attributes
rather than using a side-channel flag.

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
