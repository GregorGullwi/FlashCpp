// Test case: Template instantiation without angle brackets
// When all template parameters have defaults, Container c; should work

template<typename T = int>
struct Container {
    T value;
};

int main() {
    Container c;      // Should instantiate as Container_int
    c.value = 99;
    return 0;
}
