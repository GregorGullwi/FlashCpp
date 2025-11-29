// Test case to reproduce template default argument parser bug
// This file tests parsing order sensitivity

template<typename T = int>
struct Container {
    T value;
};

int main() {
    Container<> c;
    c.value = 42;
    return 0;
}
