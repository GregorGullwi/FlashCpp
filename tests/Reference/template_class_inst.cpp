// Class template instantiation test
// Tests using Container<int> to instantiate a class template
template<typename T>
class Container {
public:
    T value;
};

int main() {
    Container<int> c;
    c.value = 42;
    return c.value;
}

