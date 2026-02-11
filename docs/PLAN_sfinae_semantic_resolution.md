# SFINAE Semantic Member Resolution Plan

**Date:** 2026-02-11  
**Status:** Not started  
**Priority:** Medium — blocks full SFINAE support for type trait patterns

## Problem

SFINAE with trailing return types like `decltype(u->foo(), void(), true)` requires the
expression parser to **semantically resolve** member accesses when checking if a return
type is valid. Currently, the parser accepts `u->foo()` syntactically without verifying
that `foo()` exists as a member of the concrete type substituted for `U`.

This means negative SFINAE cases (where substitution should fail) are not detected:
```cpp
struct WithFoo { void foo() {} };
struct WithoutFoo {};

template<typename U>
auto check(U* u) -> decltype(u->foo(), true) { return true; }

template<typename U>
auto check(...) -> bool { return false; }

// check<WithFoo>(nullptr)    → correctly returns true (first overload)
// check<WithoutFoo>(nullptr) → INCORRECTLY returns true (should fall back to second overload)
```

## Root Cause

The expression parser in `parse_expression()` creates `MemberAccessNode` and
`FunctionCallNode` AST nodes without resolving whether the member actually exists.
Member resolution happens later during template body instantiation or codegen.

During SFINAE re-parse in `try_instantiate_template_explicit`, the infrastructure is in
place:
- `in_sfinae_context_ = true`
- `parsing_template_body_ = false`
- `current_template_param_names_` cleared
- Template parameters bound to concrete types
- Function parameters registered in scope

But the expression parser doesn't use `in_sfinae_context_` to trigger member lookup.

## Infrastructure Already Built (2026-02-11)

1. **Overload loop in `try_instantiate_template_explicit`** — iterates all overloads
   via `lookupAllTemplates()`, with `continue` for SFINAE rejection
2. **`trailing_return_type_position`** on `FunctionDeclarationNode` — saved during
   template parsing at the `->` token for re-parsing
3. **SFINAE context management** — `in_sfinae_context_`, `parsing_template_body_`,
   and `current_template_param_names_` are correctly saved/restored
4. **Parameter scope** — `register_parameters_in_scope()` makes function parameters
   visible during re-parse
5. **Same infrastructure in `try_instantiate_member_function_template_explicit`** —
   with outer template binding support for nested templates

## TODO Items

### Phase 1: Member Access Resolution in SFINAE Context
- [ ] In `parse_member_access` (or wherever `u->foo()` is parsed), when
  `in_sfinae_context_` is true, resolve the struct type of the object and check if
  the member exists
- [ ] If member doesn't exist, return `ParseResult::error()` instead of creating a
  `MemberAccessNode` — this will propagate up through `parse_expression()` →
  `parse_decltype_specifier()` → cause the SFINAE `parse_type_specifier()` to fail
- [ ] Handle both `->` and `.` member access patterns

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
