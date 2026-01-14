// Test that using 'override' keyword on a function that doesn't override anything causes compilation failure
class Base {
public:
    virtual void foo() { }
};

class Derived : public Base {
public:
    // This should trigger a compilation error: bar() does not override any base class function
    void bar() override { }
};

int main() {
    return 0;
}
