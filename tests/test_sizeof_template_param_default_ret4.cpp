// Test: sizeof in non-type template parameter default
template<typename T, size_t N = sizeof(T)>
struct test {
    char data[N];
};

int main() {
    test<int> t;  // N should be sizeof(int) = 4
    // Verify by using sizeof
    return sizeof(t.data);  // Should return 4
}
