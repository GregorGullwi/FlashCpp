// Test out-of-line operator definitions
// Pattern: ReturnType ClassName::operator=(...)

class MyClass {
    int value_;
public:
    MyClass() : value_(0) {}
    explicit MyClass(int v) : value_(v) {}
    
    // Declaration of operator=
    MyClass& operator=(const MyClass& other);
    
    int get() const { return value_; }
};

// Out-of-line definition of operator=
inline MyClass& MyClass::operator=(const MyClass& other) {
    value_ = other.value_;
    return *this;
}

int main() {
    MyClass a(42);
    MyClass b;
    b = a;
    return b.get() == 42 ? 0 : 1;
}
