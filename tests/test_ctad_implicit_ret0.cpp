// Test implicit CTAD (Class Template Argument Deduction without explicit deduction guides)
// C++17 allows constructing template classes without explicit template arguments
// when the type can be deduced from constructor arguments.

template<typename T>
class Box {
public:
    T value;
    Box(T v) : value(v) {}
    T get() const { return value; }
};

int main() {
    Box box(42);  // Should deduce Box<int>
    return box.get() == 42 ? 0 : 1;
}
