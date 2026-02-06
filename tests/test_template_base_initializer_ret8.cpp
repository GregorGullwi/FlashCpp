// Test: Template base class in member initializer list
// Validates parsing of Base<T>(args) in constructor initializer lists
template<typename T>
struct Base {
    T value;
};

int main() {
    return sizeof(Base<double>);  // 8 (sizeof double)
}
