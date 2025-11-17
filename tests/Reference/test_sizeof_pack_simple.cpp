// Simplified sizeof... test - just single case

template<typename... Args>
struct Tuple {
    static const int size = sizeof...(Args);
};

int main() {
    // Test single element pack
    int s = Tuple<int>::size;
    return s - 1;  // Should return 0 (since size is 1)
}
