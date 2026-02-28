struct Foo {
    static int* ptr;
    static constexpr int val = 42;
};

int x = 10;
int* Foo::ptr = &x;

int main() {
    // Just verify the struct compiles and val is accessible
    return Foo::val - 42;
}
