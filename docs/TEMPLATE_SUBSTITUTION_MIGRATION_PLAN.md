# Template Substitution Migration Plan

## Status: Substantially Complete

The migration described in this document has been carried out. This file is retained as a historical record and to document the final architecture.

---

## What Was Done

### TemplateArgument → TemplateTypeArg Merge (Complete)

The codebase previously had two parallel template-argument types:

| Old type | Role |
|----------|------|
| `TemplateArgument` | Lightweight type+index+value triple used in some paths |
| `TemplateTypeArg` | Rich type used in the main instantiation path |

Both are now **unified into `TemplateTypeArg`** (`src/TemplateRegistry_Types.h`). `TemplateArgument` no longer exists as a standalone struct. The lightweight triple lives on as `TemplateArgumentValue` for the few contexts that need it.

`TemplateTypeArg` covers every template argument kind:
- Type parameters (`typename T`) — `base_type`, `type_index`, qualifiers, `pointer_depth`, ...
- Non-type parameters (`int N`) — `is_value == true`, `value`, `value_type`
- Variadic packs (`typename... Args`) — `is_pack == true`
- Dependent names (un-substituted params) — `is_dependent`, `dependent_name`
- Template template parameters (`template<typename> class C`) — `is_template_template_arg`, `template_name_handle`
- Member pointers — `member_pointer_kind`

All instantiation methods in `src/Parser.h` now accept `const std::vector<TemplateTypeArg>&`.

### Template Instantiation Split Out of Parser.cpp (Complete)

The three monolithic methods originally in `Parser.cpp` now live in dedicated files:

| File | Contents | Lines |
|------|----------|-------|
| `src/Parser_Templates_Inst_ClassTemplate.cpp` | `try_instantiate_class_template`, `instantiate_full_specialization`, `instantiate_and_register_base_template` | ~6270 |
| `src/Parser_Templates_Inst_Deduction.cpp` | Template argument deduction, `populateTemplateParamSubstitutions` | ~1914 |
| `src/Parser_Templates_Inst_MemberFunc.cpp` | Member function template instantiation | ~532 |
| `src/Parser_Templates_Inst_Substitution.cpp` | Expression substitutor, `substitute_template_parameter` | ~1032 |
| `src/Parser_Templates_Instantiation.cpp` | Unity-build stub | 4 |

Additional template-related files:

| File | Contents |
|------|----------|
| `src/Parser_Templates_Params.cpp` | Template parameter list parsing, template template params |
| `src/Parser_Templates_Function.cpp` | Function template declaration |
| `src/Parser_Templates_Class.cpp` | Class template declaration |
| `src/Parser_Templates_Variable.cpp` | Variable template declaration |
| `src/Parser_Templates_Concepts.cpp` | Concept checking / `evaluateConstraint` |
| `src/Parser_Templates_Lazy.cpp` | Lazy (deferred) template body storage |
| `src/Parser_Templates_MemberOutOfLine.cpp` | Out-of-line member template definitions |
| `src/Parser_Templates_Decls.cpp` | Template declaration helpers |
| `src/Parser_Templates_Substitution.cpp` | Substitution pass |
| `src/Parser_Templates.cpp` | Top-level dispatch |

### Helper Infrastructure (Complete)

**`src/TemplateInstantiationHelper.h`** — shared utilities used by `ExpressionSubstitutor` and `ConstExpr::Evaluator`:
- `deduceTemplateArgsFromCall()` — extracts args from constructor call patterns (`__type_identity<int>{}`)
- `deduceTemplateArgsFromParamTypes()` — deduces args by matching parameter vs. argument types
- `tryInstantiateTemplateFunction()` / `tryInstantiateWithErrorInfo()` — tries qualified then simple name
- `isTemplateTemplateParameter()` — detects template template patterns
- `buildTemplateParamMap()` — builds `unordered_map<string_view, TemplateTypeArg>` from a param/arg pair

**`src/TemplateRegistry_Pattern.h`** — specialization matching and lookup:
- `toTemplateTypeArg()` — converts `TypeInfo::TemplateArgInfo` to `TemplateTypeArg`
- Thin shim overloads: `lookupSpecialization`, `evaluateConstraint`, `substitute_template_parameter`, `try_instantiate_class_template`, `get_instantiated_class_name`

**`src/TemplateRegistry_Registry.h`**, **`src/TemplateRegistry_Lazy.cpp`** — lazy instantiation registry with specialization caching.

### Template Parameter Scoping (Complete)

Template parameters no longer pollute `gTypeInfo`. Each instantiation operates through:

```cpp
// src/Parser.h
struct TemplateParamSubstitution {
    StringHandle param_name;
    bool is_value_param;
    int64_t value;
    Type value_type;
    bool is_type_param = false;
    TemplateTypeArg substituted_type;
};
InlineVector<TemplateParamSubstitution, 4> template_param_substitutions_;
InlineVector<StringHandle, 4> current_template_param_names_;
```

`ScopedState` RAII guards (`src/ParserScopeGuards.h`) save and restore these vectors across nested instantiations, so concurrent recursive instantiations are safe and parameter names from different templates never collide.

---

## Original Plan vs. Outcome

| Original Phase | Outcome |
|---------------|---------|
| A — Dependency analysis | Completed implicitly during refactoring |
| B — Extract pure helpers | Done: `TemplateInstantiationHelper.h`, `TemplateRegistry_Pattern.h` |
| C — Pass `Parser&` interface | Done: all helpers receive `Parser&` |
| D1 — Variable template migration | Done: `try_instantiate_variable_template` in dedicated file |
| D2 — Function template migration | Done: `try_instantiate_template_explicit` and deduction in dedicated files |
| D3 — Class template migration | Done: `Parser_Templates_Inst_ClassTemplate.cpp` |

The original plan proposed `TypeIndex instantiateClassTemplate(Parser& parser, ...)` free functions. The actual implementation keeps the methods on `Parser` but moves the bodies to separate translation units compiled via the unity build (`src/FlashCppUnity.h`). This achieves the same separation without introducing indirection.

---

## Success Criteria — Verified

- ✅ All 1259+ tests pass (as of 2026-03-04)
- ✅ `TemplateArgument` type removed; single `TemplateTypeArg` throughout
- ✅ Template instantiation code extracted from monolithic `Parser.cpp`
- ✅ Shared helpers in `TemplateInstantiationHelper.h` eliminate duplication
- ✅ Template parameter scoping clean — no `gTypeInfo` pollution

---

*Document originally created: 2025 (migration plan)*
*Updated: 2026-03-04 — reflects completed migration and TemplateArgument→TemplateTypeArg merge*
