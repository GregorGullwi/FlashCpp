struct Foo {
    static const int* ptr;
    static constexpr int val = 42;
};

int main() {
    return Foo::val;
}
