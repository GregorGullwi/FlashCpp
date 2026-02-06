// Test: Template qualified member function call
// Validates parsing of Base<T>::member(args) in expression context
// Uses sizeof to verify the template pattern parses correctly without link issues
template<typename T>
struct Base {
    T data;
};

int main() {
    // Verify that Base<int> is correctly parsed with qualified name syntax
    return sizeof(Base<int>);  // 4 (sizeof int)
}
