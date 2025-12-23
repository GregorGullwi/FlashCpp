// Test operator& overload - baseline without overload resolution
// This test documents the CURRENT behavior before implementing operator overload resolution

struct AddressProvider {
    int value;
    
    // Overloaded operator& - should return custom value
    // Currently NOT called by & operator (both & and __builtin_addressof behave the same)
    int* operator&() {
        static int custom_value = 99;
        return &custom_value;
    }
};

int main() {
    AddressProvider obj;
    obj.value = 42;
    
    // Get address using & operator
    // CURRENT BEHAVIOR: Gets actual address (ignores overload)
    // EXPECTED FUTURE BEHAVIOR: Should call operator& overload and get custom address
    AddressProvider* actual_addr = &obj;
    
    // __builtin_addressof should always get actual address (bypass overload)
    AddressProvider* builtin_addr = __builtin_addressof(obj);
    
    // For now, both should be the same (both get actual address)
    // After implementing overload resolution:
    // - actual_addr would point to custom_value (99)
    // - builtin_addr would point to obj.value (42)
    
    // Return the value at the address we got
    // Currently returns 42 because overload is not called
    return actual_addr->value;
}
