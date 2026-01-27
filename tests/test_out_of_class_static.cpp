template<typename T>
struct Container {
    static int value;  // Declaration only
};

template<typename T>
int Container<T>::value = 42;  // Out-of-class definition

// Expected return: 214
int main() {
    return Container<int>::value - 42;
}
