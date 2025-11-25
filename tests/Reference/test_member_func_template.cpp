// Test member function templates

struct Converter {
    // Member function template - converts to any type
    template<typename T>
    T convert(int value) {
        return value;  // Simplified - just return without cast
    }
};

int main() {
    Converter c;
    
    int i = c.convert<int>(42);      // Should return 42
    
    // For now, just test int conversion
    return i - 42;  // Should return 0
}
