// Simpler out-of-line template test
template<typename T>
class Container {
public:
    T value;
    T get();
};

// Out-of-line definition
template<typename T>
T Container<T>::get() {
    return value;
}

int main() {
    Container<int> c;
    c.value = 42;
    return c.get();
}

