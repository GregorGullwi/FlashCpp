# Namespace Table Refactor Plan

## Overview
Introduce a global namespace table backed by a ChunkedVector to replace repeated NamespacePath vector usage with stable handles. The table stores parent/child relationships and optional cached qualified names, enabling faster lookups and avoiding repeated allocations during namespace traversal and qualified-name building.

## Current Namespace Handling
- Namespace paths are represented as `NamespacePath` (`std::vector<StringType<>>`) in `/home/runner/work/FlashCpp/FlashCpp/src/SymbolTable.h` and used as keys in `namespace_symbols_`.
- The symbol table stack stores `Scope::namespace_name` and `build_current_namespace_path()` rebuilds vectors on every lookup in `/home/runner/work/FlashCpp/FlashCpp/src/SymbolTable.h`.
- `buildQualifiedName()` and related helpers in `/home/runner/work/FlashCpp/FlashCpp/src/SymbolTable.h` concatenate strings each time a qualified name is needed.
- Parsing of namespaces and inline namespaces in `/home/runner/work/FlashCpp/FlashCpp/src/Parser.cpp` creates `NamespacePath` instances for using directives, alias resolution, and inline namespace merging.

## Current Pain Points
- Frequent `NamespacePath` construction/copying for lookups, using directives, alias resolution, and inline namespace handling.
- `NamespacePath` hashing and equality checks are repeated across symbol table operations.
- Qualified-name building allocates or concatenates each time instead of reusing cached qualified paths.
- Namespace aliases and using directives store full vector paths, increasing memory churn and lookup overhead.

## Proposed Data Structures
### NamespaceHandle
- `using NamespaceHandle = uint32_t;`
- `constexpr NamespaceHandle kInvalidNamespace = 0;`
- `constexpr NamespaceHandle kGlobalNamespace = 1;` (first entry in the table).

### NamespaceEntry
```
struct NamespaceEntry {
StringHandle name;               // Component name
NamespaceHandle parent;          // Parent namespace handle
StringHandle qualified_name;     // Optional cached qualified name (0/invalid if not computed)
};
```

### NamespaceTable
- Owned by the symbol table (e.g., `SymbolTable::namespace_table_`).
- Stores entries in a `ChunkedVector<NamespaceEntry>` (see `/home/runner/work/FlashCpp/FlashCpp/src/ChunkedAnyVector.h`).
- Maintains a lookup map keyed by `(parent_handle, name_handle)` to reuse handles.
- Provides helpers:
  - `NamespaceHandle intern(StringHandle name, NamespaceHandle parent)`
  - `NamespaceHandle resolvePath(const NamespacePath&)`
  - `std::string_view qualifiedName(NamespaceHandle handle)`
  - `std::string_view buildQualifiedName(NamespaceHandle handle, StringHandle name)`

## Migration Steps
1. Add `NamespaceHandle`, `NamespaceEntry`, and `NamespaceTable` definitions (likely in `/home/runner/work/FlashCpp/FlashCpp/src/SymbolTable.h` or a new header nearby).
2. Initialize the table with a global namespace entry and store a stable `kGlobalNamespace` handle.
3. Replace `Scope::namespace_name` with `Scope::namespace_handle` and update `enter_namespace()` to intern or lookup handles instead of storing `StringType<>` directly.
4. Replace `build_current_namespace_path()` with `build_current_namespace_handle()` (or return the current handle by tracking it on scope entry/exit).
5. Update `namespace_symbols_` to key by `NamespaceHandle` instead of `NamespacePath`.
6. Update using directives, using declarations, and namespace aliases to store `NamespaceHandle` (or lightweight handle paths) instead of `NamespacePath` vectors.
7. Provide shim helpers to translate existing `NamespacePath` inputs into handles (to keep parser and AST signatures unchanged initially).
8. Update inline namespace merge logic to operate on handles and avoid rebuilding vector paths.

## Impacts on Lookup and Qualified Name Building
- `lookup_qualified()` and `lookup_qualified_all()` should accept a `NamespaceHandle` or convert `NamespacePath` to a handle via `NamespaceTable::resolvePath()`.
- `buildQualifiedName()` can be replaced with `buildQualifiedName(NamespaceHandle, StringHandle)` that uses cached `qualified_name` handles for the namespace prefix.
- `buildFullQualifiedName()` can be derived from the cached `qualified_name` on a namespace handle to avoid per-call concatenation.

## Allocation Avoidance Notes
- Use `StringHandle`/`StringTable` for namespace component names and qualified names to avoid repeated `std::string` or `StringType<>` allocations.
- Store handles in `NamespaceEntry` and provide `std::string_view` access via `StringTable::getStringView(handle)`.
- Keep `NamespacePath` in AST nodes as-is, but resolve to handles lazily using `string_view` accessors without additional `std::string` construction.

## Mapping to Existing NamespacePath Usage
- Existing `NamespacePath` values (from parser, using directives, or AST nodes) can be bridged with `NamespaceTable::resolvePath(const NamespacePath&)`.
- Keep `NamespacePath` in `QualifiedIdentifierNode` and using directive nodes, but introduce helper functions in `/home/runner/work/FlashCpp/FlashCpp/src/SymbolTable.h` to resolve handles at lookup time.
- Once the handle-based flow is stable, consider updating parser/AST nodes to store `NamespaceHandle` in parallel if it simplifies future work.

## Testing Notes
Doc-only change; no new tests are required. If the refactor is implemented later, run `make main CXX=clang++` and `tests/run_all_tests.sh` from the repo root before and after the code change.
