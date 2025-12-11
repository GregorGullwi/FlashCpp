template<typename T>
struct Container {
    static int value;  // Declaration only
};

template<typename T>
int Container<T>::value = 42;  // Out-of-class definition

int main() {
    return Container<int>::value - 42;
}
