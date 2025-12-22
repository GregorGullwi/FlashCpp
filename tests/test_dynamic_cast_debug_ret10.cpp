struct Base {
    virtual int get() { return 42; }
    virtual ~Base() {}
};

int main() {
    Base b;
    Base* pb = &b;
    Base* result = dynamic_cast<Base*>(pb);  // Same type cast
    return result ? 10 : 20;
}
