// Test: pointer-to-member type in type alias
// Tests the pattern: using Type = Res Class::*

struct MyClass {
    int data;
};

// Pointer-to-member type alias
using DataPtr = int MyClass::*;

int main() {
    DataPtr ptr = &MyClass::data;
    MyClass obj;
    obj.data = 42;
    int value = obj.*ptr;
    return value;
}
