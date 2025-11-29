// Out-of-line template test without member variables
template<typename T>
class Container {
public:
    T get();
};

// Out-of-line definition
template<typename T>
T Container<T>::get() {
    T result = 42;
    return result;
}

int main() {
    Container<int> c;
    return c.get();
}

