// Bug: Namespace-qualified variable access produces link errors
// Status: LINK ERROR - Symbol not found during linking
// Date: 2026-02-07
//
// Variables declared in namespaces cannot be accessed with qualified names.

namespace ns {
    int value = 42;
}

int main() {
    return ns::value == 42 ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Compiles but fails during linking with unresolved symbol errors.
// The code generator does not emit the symbol for namespace-qualified variables.
//
// Fix: Ensure code generation properly emits symbols for variables
// defined inside namespaces and generates correct references for
// namespace-qualified access (ns::value).
