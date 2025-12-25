// Simple test for >> splitting
template<typename T>
struct Box {
    T value;
};

int main() {
    Box<Box<int>> nested;  // This requires >> splitting
    nested.value.value = 42;
    return nested.value.value;
}
