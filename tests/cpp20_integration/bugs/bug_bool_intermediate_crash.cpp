// Bug: Boolean intermediate variables with complex logic cause crashes
// Status: CRASH - FlashCpp assertion failure in IRTypes.h
// Date: 2025-12-02
//
// Minimal reproduction case for FlashCpp crash when using boolean
// intermediate variables in complex expressions.

int test_bool_crash() {
    bool t = true;
    bool f = false;
    
    // These intermediate boolean variables cause FlashCpp to crash
    bool and_result = t && t;
    bool or_result = f || t;
    bool not_result = !f;
    
    if (and_result && or_result && not_result) {
        return 10;
    }
    return 0;
}

int main() {
    return test_bool_crash();
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 10
//
// Actual behavior (with FlashCpp):
// Crashes with:
// FlashCpp: src/IRTypes.h:2176: const T &IrInstruction::getTypedPayload() const [T = BinaryOp]: 
// Assertion `typed_payload_.has_value() && "Instruction must have typed payload"' failed.
//
// Signal: SIGABRT (Abort)
//
// Workaround:
// Use direct boolean expressions in if conditions instead of intermediate variables
