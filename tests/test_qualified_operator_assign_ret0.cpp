// Regression test: qualified ::operator= call parsing
// The parser must handle Type::operator=() in both postfix and statement contexts

struct Base {
    int x;
    Base& operator=(const Base& other) {
        x = other.x;
        return *this;
    }
};

struct Derived : Base {
    int y;
    Derived& operator=(const Derived& other) {
        Base::operator=(other);  // qualified operator= call in statement context
        y = other.y;
        return *this;
    }
};

int main() {
    Derived a;
    a.x = 10;
    a.y = 20;
    Derived b;
    b.x = 30;
    b.y = 40;
    a = b;
    return a.x + a.y - 70;  // 30 + 40 - 70 = 0
}
