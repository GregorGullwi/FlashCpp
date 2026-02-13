# Template Unification Plan

**Date:** 2025-06-15

## The Problem

When a template like `template<typename T> struct Holder` is defined inside `namespace custom_ns` and instantiated as `custom_ns::Holder<int>`, the compiler can create **two separate `TypeInfo` entries** in `gTypesByName`:

| Entry | Name | TypeIndex | Has function bodies? |
|-------|------|-----------|---------------------|
| 1 | `Holder$bb125dafdd4970ce` | e.g. 42 | Sometimes no |
| 2 | `custom_ns::Holder$bb125dafdd4970ce` | e.g. 57 | Yes |

These represent the **same C++ type** but have different `TypeIndex` values, which means:
- They produce **different mangled names** for functions that use them as parameters/return types
- Function bodies may exist on one but not the other (the current workaround propagates bodies from #2 to #1)
- Codegen iterates all types and may find the function on the wrong one

## Root Cause Analysis

The **root cause** is that `try_instantiate_class_template(template_name, args)` receives `template_name` that may be **either** `"Holder"` or `"custom_ns::Holder"` depending on the call site, and the generated name (`get_instantiated_class_name`) preserves whatever prefix was passed:

```
get_instantiated_class_name("Holder", args)              -> "Holder$bb125dafdd4970ce"
get_instantiated_class_name("custom_ns::Holder", args)   -> "custom_ns::Holder$bb125dafdd4970ce"
```

The dedup checks use these names as keys, so they **never detect** that these are the same type.

### Hash Generation (TemplateTypes.h:280-310)

The hash suffix (e.g., `$bb125dafdd4970ce`) is computed **only from the template arguments** (type args, value args, template template args). The `template_name` is NOT included in the hash. Instead, `template_name` is used as a prefix:

```cpp
// generateInstantiatedName() builds: template_name + "$" + hex_hash
builder.append(template_name);  // <-- INCLUDES NAMESPACE IF PRESENT
builder.append("$");
builder.append(hash_str);
```

So `Holder<int>` always produces the same hash suffix regardless of whether it's called as `"Holder"` or `"custom_ns::Holder"` -- but the full names differ.

### Dedup Checks That Fail

Two dedup mechanisms both fail for the cross-namespace case:

1. **`gTypesByName` lookup** (line ~11751): Uses `StringHandle` as key. `"Holder$hash"` and `"custom_ns::Holder$hash"` are different StringHandles, so the lookup misses.

2. **V2 cache** (line ~11562): The `TemplateInstantiationKeyV2` includes `base_template` (a `StringHandle`). Since `"Holder"` and `"custom_ns::Holder"` are different StringHandles, the V2 cache also misses.

### Who Passes Qualified Names?

Out of ~41 call sites of `try_instantiate_class_template`, only **5 ever pass namespace-qualified names**:

| Call Site | File | Source of qualified name |
|-----------|------|------------------------|
| Parser_Expressions.cpp:3022 | Brace init `ns::Class<T>{}` | `buildQualifiedNameFromStrings()` |
| Parser_Expressions.cpp:3202 | `ns::Template<T>::value` | Same qualified name |
| Parser_Expressions.cpp:4860 | Cascading fallback | After unqualified fails |
| Parser_Expressions.cpp:5717 | `ns::Template<T>::member` | `buildQualifiedNameFromHandle()` |
| Parser_Declarations.cpp:4574 | Base class: `class X : ns::T<int>` | `full_name` accumulated |

All other call sites pass **unqualified names**.

### Variable Templates Already Do It Right

The variable template instantiation path (Parser_Templates.cpp:10803-10806, 10936, 10990) **strips the namespace** before generating names, using `simple_template_name`. Class templates should do the same.

## The Fix: Normalize Early

The fix is to **strip the namespace prefix** from `template_name` before any name generation or cache lookups. This ensures all instantiations of the same template produce the same name regardless of how the caller refers to it.

### Phase 1: Core Normalization (Low Risk, High Impact)

**File: `src/Parser_Templates.cpp` -- `try_instantiate_class_template()`**

At the very top of the function (after the iteration limit guard), add:

```cpp
// Normalize template_name to unqualified form.
// Templates are registered under both "Holder" and "custom_ns::Holder",
// but instantiated types should use a consistent unqualified name so
// that Holder<int> instantiated from different namespaces produces the
// same TypeInfo entry (same TypeIndex, same mangled name).
std::string_view normalized_template_name = template_name;
if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
    normalized_template_name = template_name.substr(last_colon + 2);
}
```

Then use `normalized_template_name` for:
1. **Name generation**: `get_instantiated_class_name(normalized_template_name, args)` (lines ~11748, ~13721)
2. **V2 cache key**: `makeInstantiationKeyV2(normalized_handle, args)` (line ~11562)

But keep using the **original `template_name`** for:
1. **`lookupTemplate(template_name)`** -- the registry has entries under both names, but the qualified name is more specific and should be preferred for the lookup itself
2. **Specialization matching** -- `lookupExactSpecialization(template_name, ...)` and `matchSpecializationPattern(template_name, ...)`

This way the lookup uses the qualified name (more precise), but the **generated instantiation name** is always unqualified.

### Phase 2: Remove the Body Propagation Workaround

Once the normalization is in place, the workaround at lines ~15896-15924 (that copies function bodies from the namespaced version to the non-namespaced version) becomes unnecessary because there will only be **one** type entry. Remove it.

### Phase 3: Audit Callers of `get_instantiated_class_name`

Several callers use `get_instantiated_class_name(template_name, args)` independently (not through `try_instantiate_class_template`) to compute the instantiated name for `gTypesByName` lookups. These must also use the unqualified name.

| Location | Current `template_name` | Fix |
|----------|------------------------|-----|
| Parser_Templates.cpp:10480 (`instantiate_and_register_base_template`) | Could be qualified | Strip prefix before `get_instantiated_class_name` |
| Parser_Expressions.cpp:3202 | Qualified | Strip prefix |
| Parser_Expressions.cpp:5717 | Qualified | Strip prefix |
| Parser_Declarations.cpp:4574 | Qualified | Strip prefix |

Or alternatively, modify `get_instantiated_class_name` itself to always strip the namespace:

```cpp
std::string_view Parser::get_instantiated_class_name(std::string_view template_name, 
    const std::vector<TemplateTypeArg>& template_args) {
    // Always use unqualified name for consistent instantiation naming
    if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
        template_name = template_name.substr(last_colon + 2);
    }
    return FlashCpp::generateInstantiatedNameFromArgs(template_name, template_args);
}
```

This is the **simplest and safest approach** -- a single normalization point that affects all callers.

### Phase 4: Fix the V2 Cache Key

The V2 cache uses `base_template` (a `StringHandle`) as part of its key. Even after Phase 1, if some callers still use `try_instantiate_class_template("custom_ns::Holder", ...)`, the V2 key would differ. Two options:

**Option A** (preferred): Normalize the `base_template` in the V2 key to always use the unqualified StringHandle. This happens inside `try_instantiate_class_template` at line ~11562.

**Option B**: Normalize inside `makeInstantiationKeyV2` itself. Riskier because it's a general utility.

### Phase 5: Test Coverage

Add a test case that exercises the exact scenario:

```cpp
namespace test_ns {
    template<typename T>
    struct Wrapper {
        T value;
        T get() const { return value; }
        static T make(T v) { return v; }
    };
}

// Use from outside the namespace (qualified)
test_ns::Wrapper<int> w1{42};
int v1 = w1.get();
int v2 = test_ns::Wrapper<int>::make(10);

// Use as base class
struct Derived : test_ns::Wrapper<int> {
    int extra() { return get() + 1; }
};
```

Verify:
- Only ONE `TypeInfo` entry for `Wrapper$...` in `gTypesByName`
- Member functions have bodies
- Mangled names are consistent
- Static member access works

## Risk Assessment

| Phase | Risk | Rationale |
|-------|------|-----------|
| Phase 1 | **Medium** | Core change, but mirrors what variable templates already do |
| Phase 2 | **Low** | Just removing dead code after Phase 1 proves correct |
| Phase 3 | **Low-Medium** | Single function change, but affects many callers -- need test coverage |
| Phase 4 | **Low** | Cache-only change, correctness not affected if missed |
| Phase 5 | **Low** | Test-only |

## Recommended Order of Implementation

1. **Phase 3 first** -- modify `get_instantiated_class_name()` to always strip namespaces. This is the smallest, safest change with the broadest impact.
2. **Phase 1** -- normalize in `try_instantiate_class_template` for the V2 cache key.
3. **Phase 5** -- run existing tests + add new test to validate.
4. **Phase 4** -- fix V2 cache key normalization.
5. **Phase 2** -- remove the body propagation workaround (only after everything passes).

## Key Code Locations

### Name Generation
- `src/TemplateTypes.h:280-310` -- `generateInstantiatedName()` builds `name$hash`
- `src/TemplateRegistry.h:479-486` -- `generateInstantiatedNameFromArgs()` wrapper
- `src/Parser_Templates.cpp:10407-10412` -- `Parser::get_instantiated_class_name()` wrapper

### Type Registration & Dedup
- `src/AstNodeTypes.cpp:86-101` -- `add_struct_type()` creates `TypeInfo` in `gTypesByName`
- `src/Parser_Templates.cpp:11747-11755` -- `gTypesByName` dedup check
- `src/Parser_Templates.cpp:11562-11570` -- V2 cache dedup check

### Template Registry
- `src/TemplateRegistry.h:1102-1105` -- `registerTemplate()` stores by string key
- `src/TemplateRegistry.h:1188-1199` -- `lookupTemplate()` exact string match, no fallback
- Templates are dual-registered: both `"Holder"` and `"custom_ns::Holder"` point to the same node

### Body Propagation Workaround (to be removed)
- `src/Parser_Templates.cpp:15896-15924` -- copies function bodies from namespaced to non-namespaced version

### Codegen Function Search
- `src/CodeGen_Functions.cpp:449-536` -- iterates all `gTypesByName` entries to find member functions
- `src/CodeGen_Functions.cpp:468-482` -- template pattern skip logic

### Variable Template (reference implementation)
- `src/Parser_Templates.cpp:10803-10806` -- strips namespace to `simple_template_name` before name generation
