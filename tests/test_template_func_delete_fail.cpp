// Test template function with = delete parsing
// This pattern is used in standard library headers like <utility>
// This is a _fail test - it should fail to compile because we call a deleted function

template<typename T>
const T* deleted_func(const T&&) = delete;

int main() {
    // Calling a deleted function should cause a compilation error
    int x = 42;
    deleted_func<int>(static_cast<const int&&>(x));
    return 0;
}
