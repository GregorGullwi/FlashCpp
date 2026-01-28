// Test template WITHOUT explicit constructor
template<typename T>
struct Container {
    const T* const_ptr;
};

int main() {
    Container<int> c;
    return 0;
}
