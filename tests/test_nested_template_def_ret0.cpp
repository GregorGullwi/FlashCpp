// Test nested template (member function template) out-of-line definition
// This pattern is used in <algorithm> (uniform_int_distribution)
template<typename T>
struct Container {
    template<typename U>
    T convert(U u);
};

template<typename T>
template<typename U>
T Container<T>::convert(U u) {
    return T(u);
}

int main() {
    return 0;
}
