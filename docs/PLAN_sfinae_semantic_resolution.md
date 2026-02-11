# SFINAE Semantic Member Resolution Plan

**Date:** 2026-02-11  
**Status:** Phase 1 Complete  
**Priority:** Medium — blocks full SFINAE support for type trait patterns

## Completed ✓

### Phase 1: Member Access Resolution in SFINAE Context (2026-02-11)
- [x] Added `sfinae_type_map_` to Parser — maps template parameter names (e.g., "U")
  to concrete type indices during SFINAE trailing return type re-parse
- [x] Expression parser resolves template parameter types to concrete struct names
  using the map, then verifies member existence on the resolved struct
- [x] Returns `ParseResult::error()` when member not found → propagates through
  `parse_expression()` → `parse_decltype_specifier()` → SFINAE `parse_type_specifier()`
  fails → overload loop continues to next candidate
- [x] Both positive and negative SFINAE paths work for `decltype(u->foo(), void(), true)`
- [x] Both free function and member function SFINAE paths populate the map
  (including outer template parameter bindings for member functions)

### Infrastructure (2026-02-11)
- [x] Overload loop in `try_instantiate_template_explicit` via `lookupAllTemplates()`
- [x] `trailing_return_type_position` (std::optional<SaveHandle>) on `FunctionDeclarationNode`
- [x] SFINAE context management (save/restore `in_sfinae_context_`, `parsing_template_body_`,
  `current_template_param_names_`, `sfinae_type_map_`)
- [x] Parameter scope via `register_parameters_in_scope()`
- [x] Same infrastructure in `try_instantiate_member_function_template_explicit`
  with outer template binding support

## TODO Items

### Phase 2: Overload Resolution in SFINAE Context
- [ ] When `u->foo()` is a valid member but has incompatible arguments, SFINAE should
  also reject
- [ ] Support `decltype(expr)` where `expr` involves operators, conversions, etc.

### Phase 3: Template Class Type Index Bug
- [ ] `has_foo<WithFoo>` and `has_foo<WithoutFoo>` currently produce the same
  `type_index=28` when both are `Type::Struct`, causing only one class instantiation
- [ ] Fix `parse_explicit_template_arguments` to correctly differentiate struct type
  indices for template arguments
- [ ] This blocks the member-function-template SFINAE pattern
  (`has_foo<T>::check<U>(...)`)

### Phase 4: `Type::Invalid` as Default Value
- [ ] Move `Invalid` to position 0 in the `Type` enum (currently `Void` is 0)
- [ ] Audit all `Type type = Type::Void` default initializations — change to
  `Type::Invalid` where appropriate
- [ ] Update range checks like `type >= Type::Bool && type <= Type::LongDouble`
- [ ] This catches uninitialized type values that currently silently become `Void`

### Phase 5: Lazy Template Function Registration
- [ ] Member function templates (`TemplateFunctionDeclarationNode`) are currently
  always eagerly copied during class template instantiation (even when
  `use_lazy_instantiation` is true)
- [ ] Per C++ standard [temp.inst]/3: member functions of class templates are only
  instantiated when used — the current behavior is correct for function *templates*
  (they're templates themselves, not concrete functions)
- [ ] However, `LazyMemberFunctionTemplateRegistry` is never populated
  (`registerLazyMemberTemplate` is defined but never called) — this dead code
  should be cleaned up or wired in
