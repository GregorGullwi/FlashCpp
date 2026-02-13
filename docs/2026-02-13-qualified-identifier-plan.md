# Qualified Identifier Refactoring Plan

**Date:** 2026-02-13

## Motivation

After the template name unification work, template instantiation names are always unqualified (namespace-stripped). This means `TypeInfo` no longer carries information about which namespace a template was originally defined in. The immediate symptom is that `is_initializer_list_type()` cannot reliably verify that a type came from `namespace std` — it currently falls back to a static registry lookup for `"std::initializer_list"`.

More broadly, identifiers throughout the compiler are often passed as bare `std::string_view` or `StringHandle` values. Namespace context is sometimes present (in qualified names like `"std::vector"`) and sometimes absent (just `"vector"`), depending on the call site. This inconsistency leads to:

1. **Lost namespace information** — after template name unification strips prefixes, the original namespace is gone
2. **Ad-hoc qualified name construction** — callers manually concatenate `ns + "::" + name` with `StringBuilder`
3. **Duplicate lookup patterns** — functions try the qualified name first, then fall back to unqualified
4. **Ambiguity** — a bare `"initializer_list"` could be from `std`, `my_lib`, or the global namespace

## Proposed Solution: `QualifiedIdentifier` Struct

Bundle the namespace and identifier into a single struct so they always travel together:

```cpp
struct QualifiedIdentifier {
    NamespaceHandle namespace_handle;  // hierarchical namespace (e.g., std), GLOBAL_NAMESPACE for global
    StringHandle identifier_handle;    // e.g., "vector"

    bool valid() const { return identifier_handle.handle != 0; }
    bool hasNamespace() const { return !namespace_handle.isGlobal(); }

    // Construct from a possibly-qualified name like "std::vector".
    // current_ns is the namespace the code is being parsed in — used to resolve
    // unqualified names so the namespace context is never lost.
    static QualifiedIdentifier fromQualifiedName(
            std::string_view name,
            NamespaceHandle current_ns) { // current namespace can be found in the Parser class
        QualifiedIdentifier result;
        if (size_t pos = name.rfind("::"); pos != std::string_view::npos) {
            // Qualified name: resolve the namespace prefix through NamespaceRegistry
            std::string_view ns_part = name.substr(0, pos);
            result.namespace_handle = gNamespaceRegistry.getOrCreatePath(
                NamespaceRegistry::GLOBAL_NAMESPACE, {ns_part});
            result.identifier_handle = StringTable::getOrInternStringHandle(name.substr(pos + 2));
        } else {
            // Unqualified name: inherit the current namespace context
            result.namespace_handle = current_ns;
            result.identifier_handle = StringTable::getOrInternStringHandle(name);
        }
        return result;
    }
};
```

## Scope

This is a **large refactoring** that touches many subsystems. It should be done incrementally.

### Phase 1: TypeInfo Template Metadata (Small, High Impact) ✅ DONE

Replace `StringHandle base_template_name_` on `TypeInfo` with a `QualifiedIdentifier base_template_`:

- ✅ `baseTemplateName()` continues to return the unqualified `identifier_handle` (no changes to ~20 readers)
- ✅ New `sourceNamespace()` returns the `namespace_handle`
- ✅ `setTemplateInstantiationInfo()` accepts a `QualifiedIdentifier` instead of `StringHandle`
- ✅ All 7 call sites of `setTemplateInstantiationInfo` construct a `QualifiedIdentifier` from `template_name`
- ✅ For the main `try_instantiate_class_template` path, when `template_name` is unqualified, derive the namespace from `class_decl.name()` (the template declaration's struct, which stores the full qualified name)
- ✅ `is_initializer_list_type()` checks `sourceNamespace() == "std"` instead of a static registry lookup

**Files affected:** `AstNodeTypes.h`, `Parser_Templates.cpp` (7 call sites), `Parser_Types.cpp` (1 call site), `Parser_Statements.cpp`

### Phase 2: Template Registry Keys (Medium) — IN PROGRESS

Update `TemplateRegistry` to use `QualifiedIdentifier` for registration and lookup:

- ✅ `registerTemplate(QualifiedIdentifier, ASTNode)` added — internally handles dual registration (simple + qualified names)
- ✅ 2 namespace dual-registration sites in `Parser_Templates.cpp` converted to single `QualifiedIdentifier` call
- Remaining: 5 class-member dual-registration sites use `ClassName::method` qualification (not namespace scope) — need a different approach or extended `QualifiedIdentifier`
- Remaining: Lookup by `QualifiedIdentifier` for namespace-aware filtering

### Phase 3: Symbol Table Integration (Large)

Extend `SymbolTable` to use `QualifiedIdentifier`:

- `insert(QualifiedIdentifier, ASTNode)` stores namespace context
- `lookup_qualified(namespace_handle, identifier)` already exists — unify with `QualifiedIdentifier`
- Using declarations (`using std::vector`) create aliases that map `QualifiedIdentifier{global, "vector"}` → `QualifiedIdentifier{"std", "vector"}`

### Phase 4: Parser Call Sites (Large)

Update identifier parsing throughout the parser:

- `parse_qualified_identifier()` returns `QualifiedIdentifier` instead of building strings
- Template instantiation passes `QualifiedIdentifier` through the full pipeline
- Specialization matching uses `QualifiedIdentifier` for consistent lookup

### Phase 5: Codegen (Medium)

Update codegen to use `QualifiedIdentifier` for:

- Function lookup in `generateFunctionCallIr`
- Struct member access resolution
- Mangled name generation

## Known Limitations (Current State)

These are pre-existing issues that this refactoring would address:

1. **Cross-namespace function calls not rejected** — `f2::func()` compiles even when `func` is only defined in namespace `f`, not `f2`. FlashCpp doesn't validate that the function exists in the specified namespace.

2. ~~**`is_initializer_list_type` uses static registry lookup**~~ — **Fixed in Phase 1.** Now uses `TypeInfo::sourceNamespace()` to verify the type came from `namespace std`.

## Call Sites for `setTemplateInstantiationInfo`

| Line | File | Context | `template_name` |
|------|------|---------|-----------------|
| ~1267 | Parser_Templates.cpp | Forward declaration specialization | From parsed token (unqualified) |
| ~1304 | Parser_Templates.cpp | Full specialization | From parsed token (unqualified) |
| ~2551 | Parser_Templates.cpp | Partial specialization | From parsed token (unqualified) |
| ~11408 | Parser_Templates.cpp | Full specialization instantiation | May be qualified |
| ~12238 | Parser_Templates.cpp | Pattern-matched specialization | May be qualified |
| ~13848 | Parser_Templates.cpp | Primary template instantiation | May be qualified |
| ~1938 | Parser_Types.cpp | Dependent placeholder type | May be qualified |

## Recommended Order

1. **Phase 1** — highest value, smallest scope, fixes the `is_initializer_list_type` issue properly
2. **Phase 2** — natural next step, simplifies template registry
3. **Phases 3–5** — can be done incrementally as needed

## Key Code Locations

### NamespaceRegistry (`src/NamespaceRegistry.h`)

The existing `NamespaceRegistry` (global: `gNamespaceRegistry`) already provides the hierarchical infrastructure `QualifiedIdentifier` should build on:

| Helper | Purpose |
|--------|---------|
| `getOrCreateNamespace(parent, name)` | Create/find a child namespace under a parent handle |
| `getOrCreatePath(start, components)` | Walk a multi-segment namespace path (e.g., `{"std", "chrono"}`) |
| `getQualifiedName(handle)` | Return the full dotted name (e.g., `"std::chrono"`) |
| `getQualifiedNameHandle(handle)` | Same as above but returns `StringHandle` |
| `buildQualifiedIdentifier(ns, id)` | Produce `"ns::id"` as a `StringHandle` |
| `getParent(handle)` | Navigate one level up the namespace hierarchy |
| `isAncestorOf(ancestor, child)` | Walk up from `child` checking whether `ancestor` is on the path |
| `getRootNamespace(handle)` | E.g., `"std"` for `"std::chrono::duration"` |
| `getName(handle)` | Local name of a namespace (e.g., `"filesystem"` for `"std::filesystem"`) |
| `getDepth(handle)` | 0 = global, 1 = top-level, etc. |

These helpers mean `QualifiedIdentifier` does not need to reimplement namespace traversal — it can store a `NamespaceHandle` and delegate to the registry for comparisons, qualified-name construction, and ancestor checks.

### TypeInfo Template Metadata
- `src/AstNodeTypes.h:1180-1218` — `base_template_name_`, `setTemplateInstantiationInfo()`
- `src/Parser_Statements.cpp:890-918` — `is_initializer_list_type()`

### Template Registry
- `src/TemplateRegistry.h:1102-1105` — `registerTemplate()`
- `src/TemplateRegistry.h:1188-1199` — `lookupTemplate()`

### Symbol Table
- `src/SymbolTable.h:307-350` — `lookup_qualified()`

### Readers of `baseTemplateName()`
~20 call sites across `CodeGen_Expressions.cpp`, `CodeGen_Functions.cpp`, `CodeGen_Visitors.cpp`, `ConstExprEvaluator_Members.cpp`, `ExpressionSubstitutor.cpp`, `Parser_Core.cpp`, `Parser_Expressions.cpp`, `Parser_Statements.cpp`, `Parser_Templates.cpp`, `Parser_Types.cpp`
