# Timeout Analysis and Fix Plan

## Problem Summary
Template instantiation hangs at #308 (`std::__call_is_nt`) when compiling standard library headers.

## Root Cause Analysis

### Will a recursion limit fix it?
**Answer: NO - A recursion limit alone will NOT make it compile properly.**

**Why:**
1. The parser already has recursion depth checking in `try_instantiate_template` (limit: 10)
2. The parser already **skips** noexcept expressions (doesn't evaluate them)
3. The hang occurs DURING template parsing, not during noexcept evaluation

### The Real Problem

The issue is NOT noexcept evaluation - it's **template parsing of function templates**.

**Evidence:**
1. `skip_noexcept_specifier()` already exists and skips noexcept expressions
2. Recursion depth limit exists (10) but doesn't trigger
3. Hang occurs at template instantiation #308, suggesting the recursion is WITHIN a single instantiation

**Hypothesis:**
The `__call_is_nt` template has multiple overloads with different tag types:
```cpp
template<typename _Fn, typename _Tp, typename... _Args>
  constexpr bool __call_is_nt(__invoke_memfun_ref) { ... }

template<typename _Fn, typename _Tp, typename... _Args>
  constexpr bool __call_is_nt(__invoke_memfun_deref) { ... }

template<typename _Fn, typename _Tp>
  constexpr bool __call_is_nt(__invoke_memobj_ref) { ... }

template<typename _Fn, typename _Tp>
  constexpr bool __call_is_nt(__invoke_memobj_deref) { ... }

template<typename _Fn, typename... _Args>
  constexpr bool __call_is_nt(__invoke_other) { ... }
```

When parsing these templates, the parser may be:
1. Trying to parse the tag parameter types (`__invoke_memfun_ref`, etc.)
2. These tags might not be defined yet or cause lookup issues
3. Infinite loop in type resolution for the tag types

## Fix Strategy

### Option 1: Add Timeout/Iteration Limit (Short-term)
- Add iteration counter in template parsing loops
- Bail out after N iterations
- Prevents hang but may cause compilation failure

### Option 2: Fix Tag Type Handling (Medium-term)
- Ensure tag types are treated as identifiers, not requiring instantiation
- Skip body parsing for function templates until needed
- Defer template body parsing

### Option 3: Lazy Template Parsing (Long-term)
- Only parse template bodies when actually instantiated with concrete types
- Don't parse during registration phase
- Already planned in docs/LAZY_TEMPLATE_INSTANTIATION_PLAN.md

## Implementation Plan

### Phase 1: Emergency Fix (Add Iteration Limits)
1. Add iteration counter to template parsing loops
2. Add timeout after 1000 iterations
3. Log warning when limit is reached
4. Test with `<type_traits>`

### Phase 2: Improve Template Parsing
1. Skip function template bodies during registration
2. Only parse bodies during instantiation
3. Test with `<functional>`

### Phase 3: Full Lazy Instantiation
1. Implement lazy template body parsing
2. Cache parsed bodies
3. Test all standard headers

## Testing Plan

After each fix:
1. Test `tests/test_just_type_traits.cpp` (should reach >308 instantiations)
2. Test `tests/std/test_std_functional.cpp` (should compile)
3. Check watchdog logs for progress beyond #308
4. Verify no new hangs at different instantiation counts

## Expected Outcomes

### With Iteration Limit:
- Compilation may fail with error instead of hang
- But will identify WHICH template body parsing is causing the issue

### With Proper Fix:
- Should progress past instantiation #308
- May hit other issues (expected)
- Should eventually compile `<type_traits>` successfully
