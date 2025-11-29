// Test base class constructor calls

struct Base {
    int x;
    
    Base() : x(10) {}
    Base(int val) : x(val) {}
};

struct Derived : Base {
    int y;
    
    Derived() : Base(), y(20) {}
    Derived(int a, int b) : Base(a), y(b) {}
};

int main() {
    Derived d1;
    Derived d2(100, 200);
    return d1.x + d1.y + d2.x + d2.y;  // 10 + 20 + 100 + 200 = 330
}
