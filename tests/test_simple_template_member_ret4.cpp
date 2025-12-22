// Test: Simple template member access parsing
// Focus: Can the parser handle Template<T>::identifier syntax?

template<typename T>
struct Wrapper {
    int value;
};

// Test 1: Template instantiation with member access in sizeof
int test_sizeof() {
    return sizeof(Wrapper<int>::value);
}

// Test 2: Decltype with template member access
// decltype(Wrapper<int>::value) x;

int main() {
    return test_sizeof();
}
