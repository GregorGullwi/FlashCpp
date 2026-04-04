# ADL (Argument-Dependent Lookup) Audit

*Date: 2026-04-04*

## Summary

ADL is **implemented and functional** in FlashCpp with comprehensive support per
C++20 [basic.lookup.argdep]. The implementation properly separates ADL-only
symbols (hidden friends) from regular namespace symbols. This document audits
the remaining gaps.

## Implementation Overview

| Component | Location | Purpose |
|-----------|----------|---------|
| `lookup_adl_only()` | `src/SymbolTable.h:570-605` | Searches ADL-only symbols (hidden friends) in associated namespaces |
| `lookup_adl()` | `src/SymbolTable.h:613-652` | Full ADL: regular + ADL-only symbols in associated namespaces |
| `insert_into_namespace()` | `src/SymbolTable.h:550-563` | Registers hidden friends (`adl_only=true`) |
| `collect_struct_associated_namespaces()` | `src/SymbolTable.h:1136-1157` | Transitive base-class namespace collection |
| Unqualified call ADL | `src/Parser_Expr_PrimaryExpr.cpp:3410` | Augments candidates with ADL results |
| Binary operator ADL | `src/Parser_Expr_BinaryPrecedence.cpp:155` | ADL phase for operator overloads |
| Hidden friend registration | `src/Parser_Decl_StructEnum.cpp:4306-4309` | Stores friend defs in `adl_only_symbols_` |

## What Works

- **Hidden friend functions** — friend functions defined inside class bodies, found only via ADL
- **Namespace-scoped free functions** — functions in a struct's namespace found via ADL
- **Base class namespace traversal** — transitive, handles diamond inheritance
- **Enum argument ADL** — enum's enclosing namespace searched (named namespaces)
- **Operator overloads** — free-function operators found via ADL
- **ADL blocking** — variables/structs/enums suppress ADL per [basic.lookup.argdep]/1

### Test Coverage (12 tests)

| Test | Exercises |
|------|-----------|
| `test_adl_hidden_friend_ret0.cpp` | Hidden friend basics |
| `test_adl_namespace_function_ret0.cpp` | Same-namespace free function |
| `test_adl_base_class_namespace_ret0.cpp` | Base class namespace |
| `test_hidden_friend_adl_global_ret0.cpp` | Global hidden friend |
| `test_hidden_friend_adl_namespace_ret0.cpp` | Namespaced hidden friend |
| `test_hidden_friend_adl_deep_base_ret0.cpp` | Grandparent namespace (deep) |
| `test_hidden_friend_enum_adl_ret0.cpp` | Enum ADL |
| `test_hidden_friend_enum_arg_adl_ret0.cpp` | Enum as function argument |
| `test_hidden_friend_no_adl_fail.cpp` | Error when ADL args missing |
| `test_nested_struct_enum_adl_ret0.cpp` | Nested struct/enum ADL |
| `test_operator_adl_free_function_ret0.cpp` | Free-function operators |
| `test_typedef_alias_adl_ret0.cpp` | ADL through typedefs/aliases |

## Missing ADL Functionality

### 1. Range-based for loops: free-function `begin()`/`end()`

**Severity:** Medium — affects custom iterator types without member functions.

C++20 [stmt.ranged]/1 specifies that range-based for looks up `begin`/`end`
as both member functions and via ADL on the range expression. FlashCpp currently
only calls member functions.

**Location:** `src/IrGenerator_Stmt_Control.cpp:901-920`

```cpp
// Current: only member function calls
auto begin_call_expr = ... MemberFunctionCallNode(range_object_expr, begin_func_decl, ...);
```

**Impact:** Types that provide free-function `begin()`/`end()` (e.g. C arrays
wrapped in custom namespaces, or types with customization points) won't work
in range-based for loops.

**Fix:** After member lookup fails, attempt ADL lookup for `begin`/`end` with
the range expression as argument.

### 2. Enum ADL in anonymous namespaces (unverified)

**Severity:** Low — edge case.

Already documented in `docs/MISSING_FEATURES.md:437-465`. Enum ADL relies on
`TypeInfo::namespaceHandle()` returning the correct enclosing namespace. For
anonymous namespaces, this handle's propagation through `add_enum_type()` has
not been verified with a test.

### 3. `std::swap` customization point

**Severity:** Low — requires `<utility>` header support.

Generic code that calls `swap(a, b)` after `using std::swap;` relies on ADL
to find user-defined `swap` overloads. The ADL infrastructure supports this,
but no standard library integration or test exists.

### 4. Unqualified call with pointer/reference arguments

**Severity:** Low.

C++20 [basic.lookup.argdep]/2 specifies that for pointer types, the associated
namespaces include those of the pointed-to type. For reference types, the
associated namespaces are those of the referred-to type. This has not been
tested, though the current implementation collects associated namespaces from
`TypeInfo` which may already handle this when the dereferenced type is resolved.

## Intentional Deviations

### Hidden friends visible to ordinary lookup

FlashCpp also makes hidden friends findable through ordinary unqualified lookup.
Per C++20, hidden friends should only be visible via ADL. This is a
simplification that doesn't break valid code but may accept some invalid code.

### No ADL for qualified calls

Per the standard, ADL is not performed when the function name is qualified
(e.g. `ns::func(x)`). FlashCpp correctly skips ADL in this case — the
qualified lookup paths in `Parser_Expr_QualLookup.cpp` do not invoke
`lookup_adl`.

### Innermost-first namespace walk is deterministic

The new namespace walk in `resolveStructInfo` resolves ambiguity by always
preferring the innermost match, which matches C++ scoping rules. The old
suffix-scan approach could non-deterministically pick any matching type.

## Recommendations

1. **Range-for ADL** — Highest priority. Implement free-function `begin()`/`end()`
   fallback with ADL in `IrGenerator_Stmt_Control.cpp` to support iterator
   customization points.
2. **Anonymous enum ADL test** — Add a test to verify enum ADL works in
   anonymous namespaces.
3. **Pointer/reference ADL test** — Add test verifying ADL finds functions when
   argument is a pointer or reference to a namespace-scoped type.
