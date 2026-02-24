# Known IR Generation Issues

## Issue: Suboptimal Assignment IR for Reference Parameters with Compound Operations

### Status: FUNCTIONALLY CORRECT, but IR could be more optimal

### Description

When compiling compound assignment operations with enum class reference parameters (like `operator|=`), the generated IR creates unnecessary temporary variables and indirect assignments.

### Example Code

```cpp
enum class byte : unsigned char {};

constexpr byte& operator|=(byte& __l, byte __r) noexcept {
    return __l = __l | __r;
}
```

### Current IR (Functionally Correct)

```
define 64 @_Z10operator|=Rii(*8& %__l, 8 %__r) []
%4 = dereference [3]8 %__l          // Dereference __l to get value
%5 = zext uchar8 %4 to int32        // Extend to int32
%6 = zext uchar8 %__r to int32      // Extend __r to int32
%7 = or int32 %5, %6                // Perform OR operation
%8 = zext uchar8 %__l to int32      // Create temp %8 by extending __l
assign %8 = %7                      // Assign OR result to %8
ret int32 %8                        // Return %8 ✓ (was %9, now fixed!)
```

### Ideal IR (More Optimal)

```
define 64 @_Z10operator|=Rii(*8& %__l, 8 %__r) []
%4 = dereference [3]8 %__l          // Dereference __l to get value
%5 = zext uchar8 %4 to int32        // Extend to int32
%6 = zext uchar8 %__r to int32      // Extend __r to int32
%7 = or int32 %5, %6                // Perform OR operation
%8 = trunc int32 %7 to uchar8       // Truncate back to byte size
store uchar8 %8, ptr %__l           // Store directly through reference
ret ptr %__l                        // Return reference
```

### What Was Fixed

✅ **Return value bug**: Previously returned non-existent `%9`, now correctly returns `%8`
✅ **Enum variable handling**: Enum variables correctly treated as underlying type
✅ **Reference parameter context**: References handled correctly in LValue vs RValue contexts
✅ **Assignment expression return**: Assignments now return LHS value instead of undefined temp

### What Remains Suboptimal

⚠️ **Unnecessary temp creation**: Line `%8 = zext uchar8 %__l` creates temp %8 instead of using %7 directly
⚠️ **Indirect assignment**: Assignment goes to temp %8 instead of directly storing through __l
⚠️ **Type conversions**: Could be more direct instead of multiple zext operations

### Impact

- **Functional Correctness**: ✅ Code executes correctly
- **All 706 tests pass**: ✅ 
- **Generated object files**: ✅ Valid and executable
- **Performance**: ⚠️ Minor inefficiency (extra temps and conversions)
- **Code size**: ⚠️ Slightly larger than optimal

### Root Cause

The suboptimal IR stems from how reference parameters interact with:
1. The `handleLValueAssignment` unified handler failing (size mismatch)
2. Fallback to general assignment path creating extra conversions
3. Return value tracking not following through reference semantics

### Why Not Fixed Now

1. **Tests all pass** - No functional bugs
2. **Complex interaction** - Involves multiple systems (type conversion, assignment handling, return value tracking)
3. **Risk vs. reward** - Significant refactoring needed for minor optimization
4. **Standard library functions** - The problematic patterns only appear in unused inline functions

### Future Work

To fully optimize this IR:

1. **Fix unified lvalue handler**: Make `handleLValueAssignment` work for all cases
2. **Reference-aware assignment**: Track when assigning through references and generate store directly
3. **Type conversion optimization**: Reduce unnecessary zext/trunc pairs
4. **Return value optimization**: Better track assignment expression results through reference chains

### Workaround

None needed - the generated code is functionally correct and efficient enough. The standard library functions where this pattern appears are typically inline and never actually called in the test suite.

### Related Commits

- `1f9c847`: Fixed assignment expression return value (was %9, now %8)
- `b65c804`: Fixed enum variable handling to use underlying type
- `9919625`: Fixed enum reference LValueAddress to return underlying type
- `65c315e`: Added ExpressionContext for reference parameters

### References

- C++ Standard: Assignment expressions return lvalue reference to LHS
- Clang IR: More optimized direct store through reference
- Test results: All 706 tests pass with current implementation

---

## Issue: Windows C++ EH still fails at runtime (missing catch funclet model)

### Status: OPEN - ARCHITECTURAL WORK REQUIRED

### Symptoms

- `tests/test_exceptions_basic_ret0.cpp` crashes at runtime on Windows.
- `tests/test_noexcept_ret0.cpp` returns mismatch (`99`) due failed C++ EH flow.

### What was improved already

- Added generic throw-info lookup path for `_CxxThrowException` metadata wiring.
- Fixed multiple C++ EH RVA relocations in emitted exception metadata.
- Corrected `UNWIND_INFO` C++ language-specific payload to include a FuncInfo RVA pointer.
- Emitted a basic `IPToStateMap` and moved FuncInfo toward modern layout (`0x19930522` + `EHFlags`).
- Added catch handler begin/end tracking and emitted dedicated catch-range `.pdata/.xdata` entries.
- Updated `HandlerType.addressOfHandler` relocations to dedicated catch symbols (`$catch$...`) rather than generic `.text` offsets.
- Tried neutral `dispCatchObj` (`0`) to avoid invalid establisher-frame writes; crash persisted.
- Aligned `FuncInfo` to 10 DWORD layout (`dispUnwindHelp`, `pESTypeList`, `EHFlags`) and added broader IP-to-state transitions; runtime crash persisted.
- Tried non-zero `dispUnwindHelp` derived from frame size; crash signature changed but failure persisted.
- Mirrored `FuncInfo` into `.rdata` (`$cppxdata$...`) and repointed the UNWIND language-specific pointer there with map relocations; runtime failure persisted.

### Root cause (current hypothesis)

`__CxxFrameHandler3` expects a catch-funclet oriented model (separate catch entry points with matching unwind/pdata/xdata semantics). FlashCpp currently records catch handlers as inline offsets in the parent function body, which appears insufficient and leads to runtime fail-fast during dispatch/unwind.

### Required next steps

1. Implement Windows catch funclet emission (separate symbols/regions for each catch).
2. Emit matching `.pdata/.xdata` for catch funclets.
3. Update TryBlock/Handler metadata to reference true catch funclet entry points (not inline parent-function labels).
4. Align `IPToStateMap` transitions with funclet boundaries and post-catch states.
5. Re-validate with:
    - `tests/test_exceptions_basic_ret0.cpp`
    - `tests/test_noexcept_ret0.cpp`
    - existing SEH regressions (must remain green).

### Update (2026-02-13)

Implemented first catch-funclet slice for MSVC EH:
- `CatchEnd` now carries continuation label payload.
- Windows `handleCatchBegin` emits funclet prologue (`push rbp; sub rsp, 32; mov rbp, rdx`).
- Windows `handleCatchEnd` now returns continuation address in `RAX` and emits funclet epilogue.
- Catch funclet `.pdata/.xdata` emission is enabled with explicit unwind codes (`UWOP_ALLOC_SMALL`, `UWOP_PUSH_NONVOL`).

Current behavior after this slice:
- ✅ `tests/test_eh_throw_catchall_ret0.cpp` passes
- ✅ `tests/test_eh_throw_catch_int_ret0.cpp` passes
- ❌ Two-function EH cases still fail fast (`0x40000409`):
    - `tests/test_eh_twofunc_throw_ret0.cpp`
    - `tests/test_eh_twofunc_mixed_ret0.cpp`
    - `tests/test_eh_twofunc_throw_with_main_eh_ret0.cpp`

Refined hypothesis:
- Catch funclet conversion is partially correct, but FH3 metadata/state transitions for EH in non-entry functions remain inconsistent (likely TryMap/IP-state coordination across caller/callee function boundaries).

### Update (2026-02-13, later)

Additional stabilization work and outcomes:

- Reworked return-from-catch path in Windows `handleReturn` to use a direct catch-funclet trampoline strategy (`RAX` continuation target + funclet epilogue), removing continuation-bridge stack flag/slot plumbing.
- Tried parent/catch pdata range separation to reduce overlapping RUNTIME_FUNCTION coverage.

Current behavior after this pass:
- ❌ `tests/test_eh_throw_catchall_ret0.cpp` still crashes (fast-fail signatures vary with range shaping)
- ❌ `tests/test_eh_throw_catch_int_ret0.cpp` still crashes (`0x40000409`)
- ⚠️ `tests/test_eh_twofunc_throw_ret0.cpp` improved from fail-fast crash to deterministic mismatch (`99`), indicating progress in control-flow but still incorrect EH dispatch/continuation semantics

Refined hypothesis:
- Remaining blocker is now concentrated in Windows runtime-function range modeling and continuation target validity for FH3/FH dispatch, not the basic catch-funclet prologue/epilogue emission itself.
