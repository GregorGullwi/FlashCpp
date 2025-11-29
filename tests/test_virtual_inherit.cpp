// Virtual function with inheritance
struct Base {
    int x;

    Base(int a) : x(a) {}

    virtual int getValue() {
        return x;
    }
};

struct Derived : public Base {
    int y;

    Derived(int a, int b) : Base(a), y(b) {}

    int getValue() override {
        return x + y;
    }
};

int main() {
    Derived d(10, 20);
    return 0;
}

