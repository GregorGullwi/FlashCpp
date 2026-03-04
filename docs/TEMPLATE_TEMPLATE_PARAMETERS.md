# Template Template Parameters

## Current Status

Template template parameters (e.g., `template<template<typename> class Container, typename T>`) are **fully implemented and working**.

## Implementation Summary

### Template Parameter Scoping (Option A — Implemented)

The originally proposed Option A (Local Template Parameter Registry) has been implemented. Template parameters are scoped locally to their template rather than being registered in `gTypeInfo`.

**Mechanism** (`src/Parser.h`):
```cpp
// Per-instantiation substitution table kept on the Parser
struct TemplateParamSubstitution {
    StringHandle param_name;
    bool is_value_param;           // true for non-type parameters
    int64_t value;                 // For non-type parameters
    Type value_type;               // Type of the value
    bool is_type_param = false;
    TemplateTypeArg substituted_type;  // Concrete type for type parameters
};
InlineVector<TemplateParamSubstitution, 4> template_param_substitutions_;
InlineVector<StringHandle, 4> current_template_param_names_;
```

During body re-parse (`parsing_template_body_ == true`) all parameter name lookups consult `current_template_param_names_` and `template_param_substitutions_` before falling through to `gSymbolTable`, so template-local names never collide with global entries.

`ScopedState` RAII guards save and restore these vectors across nested instantiations (see `src/ParserScopeGuards.h`).

### Template Template Argument Representation

`TemplateTypeArg` (`src/TemplateRegistry_Types.h`) carries a dedicated flag for template template arguments:

```cpp
struct TemplateTypeArg {
    // ...
    bool is_template_template_arg;   // true when this arg is a template, not a type
    StringHandle template_name_handle; // e.g. "HasType", "vector"
    // ...
    static TemplateTypeArg makeTemplateTemplate(StringHandle name) {
        TemplateTypeArg arg;
        arg.is_template_template_arg = true;
        arg.template_name_handle = name;
        return arg;
    }
};
```

`TemplateInstantiationKey` (`src/TemplateTypes.h`) hashes template template args through a separate `template_template_args` vector so that specialisations keyed on different template arguments are cached separately.

### Parsing Template Template Parameters

`src/Parser_Templates_Params.cpp` — `parse_template_template_parameter_forms()` parses the nested parameter list of a template template parameter (e.g., `template<typename, typename>` in `template<template<typename, typename> class C>`).

`src/Parser_Templates_Inst_Deduction.cpp` — deduction skips `is_template_template_arg` entries when computing concrete type sizes and maps the template name into the substitution table so downstream code can call `Container<T>`.

`src/Parser_Templates_Substitution.cpp` — substitution guards (`!arg.is_template_template_arg`) prevent the substitution pass from treating a template name as a concrete type.

## Resolved Problems

### ✅ Global Type Registry Pollution

Template parameter names are no longer registered globally. Each instantiation populates its own `template_param_substitutions_` vector, which is in scope only for the duration of the body re-parse and then discarded via RAII.

### ✅ Template Parameter Scope

`current_template_param_names_` + `template_param_substitutions_` provide proper per-instantiation scoping. Multiple templates with identical parameter names (e.g., both using `Container` and `T`) instantiate without collision.

### ✅ Code Generation for Template Template Arguments

`is_template_template_arg == true` in a `TemplateTypeArg` tells instantiation code that the argument is a template name, not a concrete type. Calls like `Container<T>` are resolved at instantiation time by looking up the concrete template name stored in `template_name_handle`.

## Test Coverage

| Test file | Scenario |
|-----------|----------|
| `tests/template_template_minimal_ret0.cpp` | Minimal smoke test |
| `tests/template_template_simple_ret0.cpp` | Simple one-parameter case |
| `tests/template_template_call_ret0.cpp` | Template template used in a function call |
| `tests/template_template_just_func_ret0.cpp` | Template template on a free function |
| `tests/template_template_params_ret0.cpp` | Multiple template template parameters |
| `tests/template_template_test_ret0.cpp` | General feature test |
| `tests/template_template_with_inst_ret0.cpp` | Instantiation of template template |
| `tests/template_template_with_member_ret0.cpp` | Template template with member access |
| `tests/template_template_with_vector_ret0.cpp` | Template template with std::vector |

All nine tests pass.

## Known Limitations

### Braced Initializer Lists in Template Contexts

Braced initializer lists (`Container<T> v{1, 2, 3}`) inside template template functions are not yet supported. Use default construction plus member calls as a workaround:

```cpp
Container<T> v;   // ✅ default construction
v.push_back(1);   // ✅ member calls
```

See `docs/MISSING_FEATURES.md` for tracking.

## References

- C++ Standard: Template Template Parameters [temp.arg.template]
- Type definition: `src/TemplateRegistry_Types.h` — `TemplateTypeArg::is_template_template_arg`
- Parameter parsing: `src/Parser_Templates_Params.cpp` — `parse_template_template_parameter_forms()`
- Deduction: `src/Parser_Templates_Inst_Deduction.cpp`
- Substitution: `src/Parser_Templates_Substitution.cpp`
- Scoping: `src/Parser.h` — `TemplateParamSubstitution`, `template_param_substitutions_`

---

*Document originally created: 2025-12-13*
*Updated: 2026-03-04 — reflects completed implementation*
