// Test friend function declarations with noexcept specifier
// Tests multiple patterns that were previously failing

class MyClass {
public:
    int value;
    
    // Friend function with noexcept and inline body
    friend bool operator==(const MyClass& a, const MyClass& b) noexcept {
        return a.value == b.value;
    }
    
    // Friend function declaration with noexcept
    friend void swap(MyClass& a, MyClass& b) noexcept;
    
    // Friend function returning a pointer
    friend int* get_ptr(MyClass& c);
};

// Out-of-line friend function definition
void swap(MyClass& a, MyClass& b) noexcept {
    int tmp = a.value;
    a.value = b.value;
    b.value = tmp;
}

int* get_ptr(MyClass& c) {
    return &c.value;
}

int main() {
    MyClass a, b;
    a.value = 10;
    b.value = 20;
    swap(a, b);
    // After swap, a.value should be 20, b.value should be 10
    // Return 0 if swap worked correctly
    return (a.value == 20 && b.value == 10) ? 0 : 1;
}
