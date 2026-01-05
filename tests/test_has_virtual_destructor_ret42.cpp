// Test __has_virtual_destructor intrinsic
// Should return 0 (false) for a struct with no virtual destructor

struct Simple {
    int x;
};

struct WithVirtual {
    virtual ~WithVirtual() {}
};

int main() {
    // Test __has_virtual_destructor with Simple (should be false)
    bool simple_has_vdtor = __has_virtual_destructor(Simple);
    
    // Test __has_virtual_destructor with WithVirtual (should be true)
    bool with_virtual_has_vdtor = __has_virtual_destructor(WithVirtual);
    
    // Return 42 if both checks are correct
    if (!simple_has_vdtor && with_virtual_has_vdtor) {
        return 42;
    }
    return 0;
}
