struct Foo {
    static const int val;
};

const int Foo::val = 42;

int main() {
    return Foo::val;
}
