// Test: Global scope resolution with ::
// Status: PASS - FlashCpp correctly handles :: prefix
// Date: 2025-12-03
//
// This test verifies that FlashCpp can access global symbols when a local
// symbol has the same name using the :: prefix for global scope resolution.

int value = 100;  // Global variable

namespace NS {
    int value = 200;  // Namespace variable
}

int test_global_resolution() {
    int value = 50;  // Local variable
    
    // Access global value using ::
    int global_val = ::value;
    
    // Access namespace value  
    int ns_val = NS::value;
    
    // Access local value
    int local_val = value;
    
    // Check all values are correct
    if (global_val == 100 && ns_val == 200 && local_val == 50) {
        return 0;
    }
    return 1;
}

int main() {
    return test_global_resolution();
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// ✅ Compiles and runs successfully, returns 0
//
// Notes:
// FlashCpp correctly implements global scope resolution with the :: prefix,
// allowing access to global variables even when shadowed by local variables.
