// Test template function with = delete parsing
// This pattern is used in standard library headers like <utility>

template<typename T>
const T* deleted_func(const T&&) = delete;

int main() {
    // Just test that the deleted function declaration parses correctly
    return 0;
}
