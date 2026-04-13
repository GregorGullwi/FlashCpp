# Known Issues

### `searchReceiverHierarchy` bypasses template pattern lookup for inherited base classes

**Severity**: Low (unlikely to manifest in practice; fallback paths cover the gap)

**Introduced in**: PR #1262 (inherited member-function lookup)

The new `searchReceiverHierarchy` lambda in `tryAnnotateCallArgConversions`
(`src/SemanticAnalysis.cpp`) infers the receiver's struct type and calls
`searchStructMembers` directly.  `searchStructMembers` walks the `StructTypeInfo`
hierarchy through `base_classes`, but it only consults `StructTypeInfo::member_functions`
on each class — it does **not** check template pattern structs (names ending in
`$pattern__`).

By contrast, the existing `searchMemberContextHierarchy` path goes through a richer
resolution flow that also considers template pattern structs when an instantiation's
`StructTypeInfo` does not directly carry the member function.

**Why this is unlikely to be a real bug:**

1. Template instantiations in FlashCpp typically carry **copied member functions** from
   their patterns, so `searchStructMembers` would find them on the instantiation's
   `StructTypeInfo` directly.
2. Even if `searchReceiverHierarchy` misses a method, the code falls through to other
   resolution strategies (`searchMemberContextHierarchy`, qualified name resolution,
   `resolveCallArgAnnotationTarget`, etc.) that use the richer lookup path.  So the
   resolution would succeed on a subsequent attempt.

**When this could matter:**

If the template instantiation model changes, or if an edge case arises where an inherited
template base class only has its member functions on the pattern struct (not copied to the
instantiation), `searchReceiverHierarchy` would fail to find the method and the fallback
paths would need to cover it.

**Fix (if needed)**: Mirror the template-pattern fallback from `searchMemberContextHierarchy`
inside `searchReceiverHierarchy`, or unify both paths into a single helper that always
consults both the instantiation and the pattern struct.
