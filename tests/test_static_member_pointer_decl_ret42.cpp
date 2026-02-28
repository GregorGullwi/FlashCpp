struct Foo {
    static int* ptr;
    static constexpr int val = 42;
};

int main() {
    return Foo::val;
}
