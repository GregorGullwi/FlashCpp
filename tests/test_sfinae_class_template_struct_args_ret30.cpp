// Test: Class template correctly differentiates struct type arguments
// has_foo_class<WithFoo> and has_foo_class<WithoutFoo> should be separate instantiations
struct WithFoo { void foo() {} };
struct WithoutFoo {};

template<typename T>
struct has_foo_class {
int value;
};

int main() {
has_foo_class<WithFoo> a;
a.value = 10;
has_foo_class<WithoutFoo> b;
b.value = 20;
return a.value + b.value;  // Should be 30
}
