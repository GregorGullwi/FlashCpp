// Simple test - partial specialization with inheritance (no static members accessed)
template<typename T> 
struct Base { };

// Partial specialization with inheritance
template<typename T> 
struct Base<const T> : Base<T> { };

int main() {
    Base<const int> x;
    return 0;
}
