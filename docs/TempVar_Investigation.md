# Investigation: Replacing TempVar with Named Variables (Post-Interning)

## Executive Summary
With the String Interning infrastructure complete, the `TempVar` component in `FlashCpp` is strictly redundant. We can now replace the `TempVar` wrapper with standard `StringHandle`s, fully unifying the variable representation in the IR.

## Current Architecture (Post-String Interning)
- **Environment**: The compiler now uses `StringHandle` (32-bit packed ID) for variable names. `IrOperand` uses `StringHandle`.
- **Legacy Artifact**: `TempVar` (in `IRTypes.h`) still exists as a distinct variant in `IrValue` wrapping a `size_t`.
- **Redundancy**: The backend now handles `StringHandle` lookups efficiently. Maintaining a separate `TempVar` path requires duplicate logic for mapping "TempVar Index" to stack slots vs "StringHandle" to stack slots.
- **Allocation Issues**: Currently, `TempVar` allocation is somewhat loose, often manually incrementing a counter (`var_counter.next()`) or calling `TempVar(++counter)` in line.

## Proposed Solution
Eliminate `TempVar` entirely and use `StringHandle` for all identifiers, including compiler-generated temporaries.

### Implementation Details
1.  **Naming Convention**: Use numeric strings (e.g., "1", "2", "3") for temporaries.
    - **Collision Safety**: C++ identifiers cannot start with a digit. Therefore, a variable named "1" is a safe, compile-time constant name.
2.  **Type Simplification**:
    - Remove `TempVar` from `IrValue` and `IrOperand` variants.
    - `IrValue` becomes simply `std::variant<unsigned long long, double, StringHandle>`.
3.  **Logic Unification**:
    - Delete `getStackOffsetFromTempVar` logic in `IRConverter.h`.
    - All stack slot resolution is done via a single `std::unordered_map<StringHandle, VariableInfo>`.

### Helper Function for Temp Allocation
To address the "loose allocation" issue, we will introduce a dedicated helper method in `AstToIr` (and potentially other generators) to standardize temp creation.

```cpp
class AstToIr {
    // ...
    uint32_t temp_var_counter_ = 1;

    // Standard way to get a new temporary variable handle.
    // NOTE: This ONLY generates a unique name/handle (e.g., "1", "2").
    // It does NOT allocate stack space. Stack space is allocated lazily by the 
    // Backend (IRConverter) when it encounters the variable being used as a result.
    StringHandle allocateTempVar() {
        // Optimization: Use pre-interned handles for small numbers (1-255)
        // to avoid string creation.
        uint32_t id = temp_var_counter_++;
        if (id < 256) {
             return gPreInternedSmallInts[id];
        }
        // Fallback: Create handle for larger numbers
        return StringTable::createStringHandle(std::to_string(id));
    }
    // ...
};
```

### Stack Allocation Strategy
Stack space for these "named temporaries" is allocated automatically by the Backend (`IRConverter`) during the `calculateFunctionStackSpace` pass (Lines 4251+ in `IRConverter.h`):
1.  **Discovery**: The function scans all IR instructions.
2.  **Size Inference**: For instructions that produce a result (e.g., `AddOp`, `CallOp`), it checks the result variable.
    - If it's a known variable (declared via `VariableDecl`), it has a size.
    - If it's a temporary (now a `StringHandle`), the backend infers its size from the operation (e.g., `CallOp.return_size_in_bits` or `BinaryOp.lhs.size_in_bits`).
3.  **Slot Assignment**: It populates `temp_var_sizes` and eventually `variable_scopes` with an offset.
    - *Action needed*: Verify `calculateFunctionStackSpace` correctly handles `StringHandle` results exactly as it handled `TempVar` results (inferring size from the producing instruction).

## Benefits
1.  **Codebase Simplification**: Removes the `TempVar` struct and its specific helper methods.
2.  **Unified Backend Logic**: A single code path for `handleStore`, `handleLoad`, `handleAddressOf`, etc.
3.  **Centralized Control**: The `allocateTempVar()` helper ensures consistent naming.

## Phased Implementation Plan

### Phase 1: Preparation & Pre-Interning
*Goal: Ensure numeric handles are ready for use.*
1.  **StringTable Optimization**: Add a static/startup step to pre-intern common strings "0" through "255".
2.  **Verify Collision Safety**: Add a unit test to confirm that the Parser correctly rejects user variables starting with digits.

### Phase 2: IR Refinement (Transition)
*Goal: Switch the frontend to generate Handles for temps.*
1.  **Implement Allocation Helper**: Add the `allocateTempVar()` helper method to `AstToIr`.
2.  **Update `AstToIr` Context**: Replace all `TempVar` constructor calls with `allocateTempVar()`.
3.  **Update `TempVar` Struct**: Temporarily modify `struct TempVar` to wrap a `StringHandle`.

### Phase 3: The "Great Unification" (Removal)
*Goal: Remove the distinct type.*
1.  **Update `IrOperand`**: Remove `TempVar` from the `std::variant`.
2.  **Update `IrTypes.h`**: Remove the `TempVar` struct definition entirely.
3.  **Fix Compilation**: Replace `if (holds<TempVar>)` with `StringHandle` checks.

### Phase 4: Backend Cleanup
*Goal: Delete redundant backend logic.*
1.  **Refactor `calculateFunctionStackSpace`**: Ensure it treats `StringHandle`-based result operands as temporaries that need size tracking (just like it did for `TempVars`).
2.  **Refactor `IRConverter`**: Remove `getStackOffsetFromTempVar` and rely on `StringHandle` maps.
