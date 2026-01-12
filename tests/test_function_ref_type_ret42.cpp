// Test function reference types in type aliases and template arguments
// This tests the new feature allowing types like int(&)() as template arguments

// Function that returns 42
int get_value() { return 42; }

// Type alias for function reference type
using func_ref_t = int (&)();  // reference to function returning int
using func_ptr_t = int (*)();  // pointer to function returning int

// Use the type aliases
func_ref_t make_ref() { return get_value; }
func_ptr_t make_ptr() { return &get_value; }

int main() {
    // Test that we can use function reference types
    func_ref_t ref = get_value;
    func_ptr_t ptr = &get_value;
    
    // Call through the reference and pointer
    int val1 = ref();
    int val2 = ptr();
    
    // Both should return 42
    if (val1 != 42) return 1;
    if (val2 != 42) return 2;
    
    return 42;  // Return 42 to indicate success
}
