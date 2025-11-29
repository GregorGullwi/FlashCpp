// Test sizeof... operator with parameter packs
// sizeof... returns the number of elements in a parameter pack

template<typename... Args>
struct Tuple {
    // Store pack size as a static const member
    static const int size = sizeof...(Args);
};

int main() {
    // Empty pack: sizeof...(Args) = 0
    int empty_size = Tuple<>::size;
    
    // Single element: sizeof...(Args) = 1
    int single_size = Tuple<int>::size;
    
    // Three elements: sizeof...(Args) = 3
    int triple_size = Tuple<int, float, bool>::size;
    
    // Test that we get the right values
    return (empty_size == 0 && single_size == 1 && triple_size == 3) ? 0 : 1;
}
