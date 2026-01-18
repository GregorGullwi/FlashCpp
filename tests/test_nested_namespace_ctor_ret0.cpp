// Test out-of-line constructors in nested namespaces
// Previously failed due to StringHandle interning issue

namespace outer {
namespace inner {

class Foo {
    int value_;
public:
    Foo();
    int get() const { return value_; }
};

// Out-of-line constructor in nested namespace
inline Foo::Foo() : value_(42) { }

}
}

int main() {
    outer::inner::Foo f;
    return f.get() == 42 ? 0 : 1;
}
