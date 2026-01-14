// Test that error is reported when trying to override a final function
// This test should compile but produce an error message at compile-time
// The error message should mention "cannot override final function"

class Base {
public:
    virtual void foo() final { }
    virtual void bar() { }  // Not final - can be overridden
};

class Derived : public Base {
public:
    // This should trigger an error: cannot override final function 'foo'
    void foo() override { }
    
    // This is fine - bar() is not final
    void bar() override { }
};

int main() {
    Derived d;
    d.bar();
    return 0;
}
