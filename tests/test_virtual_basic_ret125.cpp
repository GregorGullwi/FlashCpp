// Test 1: Simple virtual function
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

int test1() {
    Derived d(10, 20);
    return d.getValue();  // Should return 30
}

// Test 2: Virtual destructor
struct Base2 {
    int value;
    
    Base2(int v) : value(v) {}
    
    virtual ~Base2() {
        // Virtual destructor
    }
};

struct Derived2 : public Base2 {
    int extra;
    
    Derived2(int v, int e) : Base2(v), extra(e) {}
    
    ~Derived2() override {
        // Override virtual destructor
    }
};

int test2() {
    Derived2 d(5, 10);
    return d.value + d.extra;  // Should return 15
}

// Test 3: Multiple virtual functions
struct Shape {
    virtual int area() {
        return 0;
    }
    
    virtual int perimeter() {
        return 0;
    }
};

struct Rectangle : public Shape {
    int width;
    int height;
    
    Rectangle(int w, int h) : width(w), height(h) {}
    
    int area() override {
        return width * height;
    }
    
    int perimeter() override {
        return 2 * (width + height);
    }
};

int test3() {
    Rectangle r(5, 10);
    return r.area() + r.perimeter();  // Should return 50 + 30 = 80
}

int main() {
    int result = 0;
    result += test1();  // 30
    result += test2();  // 15
    result += test3();  // 80
    return result;      // Should return 125
}

