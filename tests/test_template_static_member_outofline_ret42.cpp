// Test static member variable definition outside template class body
template<typename T>
struct Container {
    static constexpr int value = 42;
};

// Out-of-class definition of static member
template<typename T>
constexpr int Container<T>::value;

int main() {
    return Container<int>::value;
}
