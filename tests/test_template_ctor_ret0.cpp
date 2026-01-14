// Test template constructor parsing (minimal - parse only)
// This tests the fix for parsing template member constructors

template<typename T>
struct Box {
    T value;
    
    Box() : value() {}
    Box(const T& v) : value(v) {}
    
    // Template constructor - the key pattern being tested
    template<typename U>
    Box(const Box<U>& other) : value(other.value) {}
};

int main() {
    Box<int> b;
    b.value = 42;
    return b.value == 42 ? 0 : 1;
}
