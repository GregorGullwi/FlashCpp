// Test inline template member function
template<typename T>
class Container {
public:
    T value;
    T get() {
        return value;
    }
};

int main() {
    Container<int> c;
    c.value = 42;
    return c.get();
}

