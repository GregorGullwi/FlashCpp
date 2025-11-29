// Test template with const T* member only
template<typename T>
struct Container {
    const T* const_ptr;
};

int main() {
    Container<int> c;
    return 0;
}
