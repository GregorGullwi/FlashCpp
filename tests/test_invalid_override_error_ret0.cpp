// Test that error is reported when 'override' keyword is used but no base class function to override
// This test should compile but produce an error message at compile-time

class Base {
public:
    virtual void foo() { }
};

class Derived : public Base {
public:
    void foo() override { }  // Valid: foo() exists in base
    
    // This should trigger an error: bar() does not override any base class function
    void bar() override { }
};

int main() {
    Derived d;
    d.foo();
    return 0;
}
