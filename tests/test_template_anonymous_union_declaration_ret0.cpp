// Test case: Template with anonymous union declaration works
// Status: ✅ PASSES - Anonymous unions in templates can be declared

template<typename T>
struct Container {
    union {
        char dummy;
        T value;
    };
};

int main() {
    Container<int> c;
    return 0;
}
