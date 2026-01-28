// Test template with explicit constructor but no usage
template<typename T>
struct Container {
    const T* const_ptr;
    Container() : const_ptr(nullptr) {}
};

int main() {
    Container<int> c;
    return 0;
}
