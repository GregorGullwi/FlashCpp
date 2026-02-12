// Test partial specialization with truncated body
template <typename T>
struct Base {
    T value;
};

template <typename T>
struct Base<T*> {
    T* ptr;
    // Missing closing brace
