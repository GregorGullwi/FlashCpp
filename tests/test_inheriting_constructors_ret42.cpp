// Test inheriting constructors syntax: using Base::Base;

struct Base {
    int value;
    Base() : value(40) {}
    Base(int v) : value(v) {}
};

struct Derived : Base {
    using Base::Base;
};

int main() {
    Derived d_default;
    Derived d_value(2);
    (void)d_default;
    (void)d_value;
    return d_value.value + d_default.value;
}
