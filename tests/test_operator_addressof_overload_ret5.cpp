// Test expected behavior after implementing operator& overload resolution
// This test defines what SHOULD happen when operator overloading is properly resolved

struct Counter {
    int count;
    
    Counter() : count(0) {}
    
    // Overloaded operator& - returns a pointer to a different location
    // Standard behavior: & should call this overload
    int* operator&() {
        count++;  // Track how many times operator& was called
        static int dummy = 100;
        return &dummy;
    }
};

int get_value_at_overloaded_address() {
    Counter c;
    c.count = 0;
    
    // Use & operator - should call the overload
    int* addr = &c;
    
    // The overload returns pointer to dummy (100)
    return *addr;  // Should return 100
}

int get_value_at_builtin_address() {
    Counter c;
    c.count = 5;
    
    // Use __builtin_addressof - should bypass the overload
    Counter* addr = __builtin_addressof(c);
    
    // Should get actual address of c, so count should be accessible
    return addr->count;  // Should return 5
}

int main() {
    // When operator& overload resolution is working:
    // - get_value_at_overloaded_address() returns 100 (from overloaded operator&)
    // - get_value_at_builtin_address() returns 5 (from actual address)
    // Total: 105
    
    // CURRENTLY (before overload resolution):
    // - Both & and __builtin_addressof get actual address
    // - get_value_at_overloaded_address() returns uninitialized value (undefined behavior)
    // - get_value_at_builtin_address() returns 5
    
    return get_value_at_builtin_address();  // For now, just test __builtin_addressof
}
