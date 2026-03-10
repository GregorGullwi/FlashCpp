# Auto Return Mangling Follow-Up Plan

**Date**: 2026-03-10
**Status**: In progress — parser-side finalization refactor started, validation still pending

## Current status

The targeted `Type::Auto` mangling `InternalError(...)` checks were reverted in `src/NameMangling.h`
to restore the previous best-effort fallback behavior.

That revert is a workaround only. The real bug is that unresolved `auto` return types can still
reach mangling in some parser/template-instantiation paths.

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
- added targeted regression tests:
  - `tests/test_auto_member_delayed_ret42.cpp`
  - `tests/test_friend_auto_delayed_ret42.cpp`

Still remaining before this plan is done:

- finish auditing all template-class body-attachment paths so every `set_definition(...)` site follows the same finalize-then-mangle invariant
- build and run focused regression coverage, then broader test coverage
- once validation is green, remove the temporary `Type::Auto` mangling fallback from `src/NameMangling.h`

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

### 3. Defaulted template member functions can retain `Type::Auto` through instantiation

In `src/Parser_Templates_Class.cpp:3466-3482`:

- defaulted member functions get an empty body
- no auto return deduction happens

In `src/Parser_Templates_Inst_MemberFunc.cpp:321-381`:

- the instantiated return type is copied/substituted from the template declaration
- if the original return type is `Type::Auto`, it stays `Type::Auto`
- for functions without a stored template body position, mangling happens immediately via
  `compute_and_set_mangled_name(new_func_ref);`

This is the most likely root cause for historical failures like
`test_spaceship_template_ret127.cpp`.

### 4. Codegen still has a secondary unresolved-return hazard

In `src/CodeGen_Expr_Operators.cpp:1237-1263`:

- codegen locally treats defaulted `operator<=>` with `Type::Auto` as `Type::Int`
- but still passes the original `return_type_node` into `generateMangledNameForCall(...)`

So even when codegen has a fallback operational type, mangling can still see unresolved `auto`.

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

At that point, the temporary best-effort `Type::Auto -> int` mangling fallback can be removed safely.

## Suggested regression coverage

Add or preserve targeted tests for:

- defaulted template `operator<=>` (`test_spaceship_template_ret127.cpp`)
- defaulted non-template `operator<=>`
- ordinary free/member functions with deduced `auto` return types
- template member functions whose return type becomes concrete only at instantiation
- both MSVC and Itanium mangling paths when possible