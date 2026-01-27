template<typename T>
struct Container {
    static int value;
};

template<typename T>
int Container<T>::value = 42;

int main() {
    int x = Container<int>::value;
    return x - 42;
}
