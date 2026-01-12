// Test function pointer types in type aliases
// Note: Function references (int (&)()) have a known issue with code generation
// This test focuses on function pointer type aliases which work correctly

// Function that returns 42
int get_value() { return 42; }

// Type alias for function pointer type
using func_ptr_t = int (*)();  // pointer to function returning int

// Use the type alias
func_ptr_t make_ptr() { return get_value; }

int main() {
    // Test that we can use function pointer type aliases
    func_ptr_t ptr = get_value;  // Function name decays to pointer
    
    // Call through the pointer
    int val = ptr();
    
    // Should return 42
    if (val != 42) return 1;
    
    return 0;  // Return 0 to indicate success
}
