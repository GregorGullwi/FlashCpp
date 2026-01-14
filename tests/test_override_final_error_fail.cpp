// Test that attempting to override a final function causes compilation failure
class Base {
public:
    virtual void foo() final { }
};

class Derived : public Base {
public:
    // This should trigger a compilation error: cannot override final function 'foo'
    void foo() override { }
};

int main() {
    return 0;
}
