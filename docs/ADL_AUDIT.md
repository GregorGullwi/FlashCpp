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

### Test Coverage (16 tests)

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
| `test_range_for_adl_begin_end_ret0.cpp` | Range-for with free-function begin/end via ADL |
| `test_range_for_adl_auto_ret0.cpp` | Range-for ADL with auto type deduction |
| `test_adl_pointer_arg_ret0.cpp` | ADL with pointer and reference arguments |
| `test_adl_anonymous_enum_ret0.cpp` | Enum ADL in anonymous namespaces |

## Missing ADL Functionality

### ~~1. Range-based for loops: free-function `begin()`/`end()`~~ ✅ DONE

**Implemented.** After member lookup fails, ADL is used to find free-function
`begin()`/`end()` in the associated namespaces of the range expression type.
Both the semantic analysis (`normalizeRangedForLoopDecl`) and IR generation
(`visitRangedForBeginEnd`) paths support the fallback. The resolved ADL
functions are stored in `RangedForStatementNode` for cross-phase communication.

**Tests:** `test_range_for_adl_begin_end_ret0.cpp`, `test_range_for_adl_auto_ret0.cpp`

### ~~2. Enum ADL in anonymous namespaces~~ ✅ VERIFIED

**Verified** with `test_adl_anonymous_enum_ret0.cpp`. The `TypeInfo::namespaceHandle()`
correctly propagates through `add_enum_type()` for anonymous namespaces.

### 3. `std::swap` customization point

**Severity:** Low — requires `<utility>` header support.

Generic code that calls `swap(a, b)` after `using std::swap;` relies on ADL
to find user-defined `swap` overloads. The ADL infrastructure supports this,
but no standard library integration or test exists.

### ~~4. Unqualified call with pointer/reference arguments~~ ✅ VERIFIED

**Verified** with `test_adl_pointer_arg_ret0.cpp`. The current implementation
correctly collects associated namespaces from the pointed-to or referred-to
type because `TypeSpecifierNode::type_index()` returns the base type index,
and `lookup_adl` uses `tryGetTypeInfo()` on that index to find the struct's
namespace.

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

1. ~~**Range-for ADL** — Highest priority.~~ ✅ Done.
2. ~~**Anonymous enum ADL test** — Add a test to verify enum ADL works in
   anonymous namespaces.~~ ✅ Done.
3. ~~**Pointer/reference ADL test** — Add test verifying ADL finds functions when
   argument is a pointer or reference to a namespace-scoped type.~~ ✅ Done.
