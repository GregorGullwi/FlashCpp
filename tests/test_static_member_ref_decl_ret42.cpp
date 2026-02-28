struct Foo {
    static int& ref;
    static constexpr int val = 42;
};

int main() {
    return Foo::val;
}
