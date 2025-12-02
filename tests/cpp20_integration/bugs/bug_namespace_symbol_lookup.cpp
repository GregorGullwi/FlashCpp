// Bug: Namespace symbol lookup causes symbol table crashes
// Status: CRASH - Symbol not found during code generation
// Date: 2025-12-02
//
// Minimal reproduction case for FlashCpp crash when using
// variables declared in namespaces.

namespace TestNamespace {
    int namespace_value = 100;
    
    int namespace_function() {
        return 50;
    }
}

int test_namespace() {
    using namespace TestNamespace;
    
    int r1 = namespace_value;       // This causes the crash
    int r2 = namespace_function();  // This also causes issues
    
    return (r1 == 100) && (r2 == 50) ? 0 : 1;
}

int main() {
    return test_namespace();
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Crashes with:
// [ERROR][Codegen] Symbol 'namespace_value' not found in symbol table during code generation
// FlashCpp: src/CodeGen.h:3760: Assertion `false && "Expected symbol to exist"' failed.
//
// Signal: SIGABRT (Abort)
//
// Workaround:
// Avoid using namespaces with variables, or use fully qualified names
