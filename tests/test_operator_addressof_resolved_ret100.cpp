// Test operator& overload resolution - should call the overload
// This demonstrates standard-compliant behavior where & calls operator& if overloaded

struct CustomAddressable {
    int value;
    
    // Overloaded operator& - returns a pointer to a static location
    int* operator&() {
        static int custom_location = 100;
        return &custom_location;
    }
};

int main() {
    CustomAddressable obj;
    obj.value = 42;
    
    // Use & operator - should call operator& overload
    int* p = &obj;
    
    // The overload returns pointer to custom_location (100)
    // So dereferencing p should give us 100
    return *p;  // Should return 100
}
