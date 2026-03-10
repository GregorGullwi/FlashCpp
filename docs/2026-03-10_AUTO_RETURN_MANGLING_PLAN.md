# Auto Return Mangling Follow-Up Plan

**Date**: 2026-03-10
**Status**: In progress — parser-side finalization refactor landed, focused validation green, full-suite validation running

## Current status

The parser-side finalization refactor is now in place and the temporary `Type::Auto -> int`
name-mangling fallback has been removed again from `src/NameMangling.h`.

At this point, unresolved `auto` reaching mangling is treated as a hard compiler bug rather than
being papered over with a best-effort symbol.

## Progress update (current branch)

Completed so far:

- added parser-side helpers to centralize post-definition work:
  - `copy_function_properties(...)`
  - `create_defaulted_member_function_body(...)`
  - `finalize_function_signature_after_definition(...)`
  - `finalize_function_after_definition(...)`
- switched ordinary parsed function definitions to:
  - `set_definition(...)`
  - finalize signature / deduce return type
  - mangle afterwards
- switched delayed function-body parsing to the same ordering, with forced mangled-name recomputation
- switched defaulted non-template member functions to create the synthetic body first and finalize afterwards
- switched template instantiation paths to reuse centralized property copying and only mangle after finalization when a body is already present
- updated the `operator<=>` codegen fallback site to pass a resolved temporary return-type node into `generateMangledNameForCall(...)` instead of the original unresolved `auto` node
- fixed ADL hidden-friend lookup for the global namespace so delayed friend definitions can resolve to the finalized declaration before codegen/mangling
- added targeted regression tests:
  - `tests/test_auto_member_delayed_ret42.cpp`
  - `tests/test_friend_auto_delayed_ret42.cpp`
- added another targeted regression for out-of-line class-template member auto return:
  - `tests/test_template_member_auto_outofline_ret42.cpp`
- removed the temporary `Type::Auto` mangling fallback from both MSVC and Itanium manglers after the ordering fixes were in place
- fixed class-template out-of-line member finalization so body-based auto deduction runs with live member/function context
- fixed `get_expression_type(MemberAccessNode)` for implicit `this->member` cases during deduction/finalization
- validated focused regressions successfully:
  - `test_friend_auto_delayed_ret42.cpp`
  - `test_auto_member_delayed_ret42.cpp`
  - `test_spaceship_template_ret127.cpp`
  - `test_adl_hidden_friend_ret0.cpp`
  - `test_template_member_auto_outofline_ret42.cpp`

Still remaining before this plan is done:

- finish the long full-suite validation run and confirm final exit status
- do one last audit pass for any remaining non-finalized mangling entry points if the full suite exposes one

## Root cause summary

The core issue is **ordering**:

1. a function declaration still has return type `Type::Auto`
2. `compute_and_set_mangled_name(...)` is called
3. return-type deduction or specialization happens later, or not at all

This is architecturally backwards. Mangling should only happen after the function signature is fully
materialized.

## Concrete problem paths

### 1. Normal function definitions already mangle before auto deduction

In `src/Parser_Decl_FunctionOrVar.cpp:883-888`:

- `compute_and_set_mangled_name(final_func_decl);`
- `final_func_decl.set_definition(*block);`
- `deduce_and_update_auto_return_type(final_func_decl);`

This ordering is wrong for any function with deduced return type.

### 2. Defaulted non-template member functions special-case `<=>` bodies but still mangle too early

In `src/Parser_Decl_StructEnum.cpp:1980-2008`:

- defaulted member functions are marked implicit
- a synthetic body is created
- `operator<=>` gets a synthetic `return 0;`
- `compute_and_set_mangled_name(member_func_ref);` runs **before** `set_definition(block_node)`
- no `deduce_and_update_auto_return_type(...)` call is made there

### 3. Defaulted template member functions could retain `Type::Auto` through instantiation

In `src/Parser_Templates_Class.cpp:3466-3482`:

- defaulted member functions get an empty body
- no auto return deduction happens

In `src/Parser_Templates_Inst_MemberFunc.cpp:321-381`:

- the instantiated return type is copied/substituted from the template declaration
- if the original return type is `Type::Auto`, it stays `Type::Auto`
- for functions without a stored template body position, mangling happens immediately via
  `compute_and_set_mangled_name(new_func_ref);`

This was a root cause for failures like `test_spaceship_template_ret127.cpp` and is now covered by
the parser-side finalization changes plus targeted regression runs.

### 4. Codegen had a secondary unresolved-return hazard

In `src/CodeGen_Expr_Operators.cpp:1237-1263`:

- codegen locally treats defaulted `operator<=>` with `Type::Auto` as `Type::Int`
- but still passes the original `return_type_node` into `generateMangledNameForCall(...)`

That call site now passes a resolved temporary type node, so codegen no longer reintroduces the
unresolved-return problem on that path.

## Follow-up implementation plan

### Phase 1: Fix parser ordering for ordinary functions

1. For parsed function definitions, attach the body first.
2. Run `deduce_and_update_auto_return_type(...)` before mangling.
3. Call `compute_and_set_mangled_name(...)` only after the return type is finalized.

### Phase 2: Fix defaulted member-function handling

1. Centralize defaulted-member finalization in a helper.
2. For defaulted `operator<=>`, synthesize a body or directly materialize the intended return type.
3. Ensure the sequence is:
   - create/set definition
   - deduce or materialize return type
   - mangle

### Phase 3: Fix template member instantiation

1. When instantiating a member function with `Type::Auto` return type, do not mangle immediately.
2. If the instantiated function has a body, set the substituted body and deduce the return type first.
3. If the function is defaulted and has no stored body position, run a dedicated defaulted-member
   return-type materialization step before mangling.

### Phase 4: Remove codegen dependency on unresolved return types

1. Stop passing unresolved `return_type_node` values into `generateMangledNameForCall(...)`.
2. Use the finalized function declaration signature or a fully resolved temporary type node.

## Expected end state

By the time mangling runs:

- no function signature should still use `Type::Auto` as a return type
- defaulted/template `operator<=>` declarations should already have a concrete return type
- codegen should not need to guess a return type just to emit a call

That end state is now intended to be enforced directly: unresolved `Type::Auto` reaching mangling
throws instead of silently degrading to `int`.

## Suggested regression coverage

Add or preserve targeted tests for:

- defaulted template `operator<=>` (`test_spaceship_template_ret127.cpp`)
- defaulted non-template `operator<=>`
- ordinary free/member functions with deduced `auto` return types
- template member functions whose return type becomes concrete only at instantiation
- both MSVC and Itanium mangling paths when possible