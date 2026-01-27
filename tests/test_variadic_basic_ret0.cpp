// Basic variadic template test - parameter pack parsing
// Tests: typename... Args parameter pack declaration

template<typename... Args>
struct Tuple {
    // Empty for now - just testing parameter pack parsing
};

int main() {
    // Test instantiation with zero arguments
    Tuple<> empty;
    
    // Test instantiation with one argument
    Tuple<int> single;
    
    // Test instantiation with multiple arguments
    Tuple<int, float, bool> triple;
    
    return 0;
}
