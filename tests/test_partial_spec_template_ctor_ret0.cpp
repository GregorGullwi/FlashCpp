// Test: Template constructor detection in partial specializations
// Verifies that template constructors work inside partial specializations
// where the constructor name matches the base template name.

template<typename T>
class Container {
public:
    Container() : val_{} {}
    T val_;
};

template<typename T>
class Container<const T> {
public:
    Container() : val_{} {}
    template<typename U> Container(const Container<U>&) : val_{} {}
    T val_;
};

int main() {
    Container<int> ci;
    Container<const int> cci;
    Container<const int> cci2(ci);
    return 0;
}
