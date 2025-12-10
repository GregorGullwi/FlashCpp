// Test: Struct with lvalue reference members
// This tests a common pattern in standard library (like std::reference_wrapper)

struct IntRefHolder {
    int& ref;
    IntRefHolder(int& r) : ref(r) {}
};

struct CharRefHolder {
    char& ref;
    CharRefHolder(char& r) : ref(r) {}
};

struct ShortRefHolder {
    short& ref;
    ShortRefHolder(short& r) : ref(r) {}
};

struct DoubleRefHolder {
    double& ref;
    DoubleRefHolder(double& r) : ref(r) {}
};

struct Point {
    int x;
    int y;
};

struct StructRefHolder {
    Point& ref;
    StructRefHolder(Point& r) : ref(r) {}
};

// Template version
template<typename T>
struct RefWrapper {
    T& ref;
    RefWrapper(T& r) : ref(r) {}
};

int main() {
    // Test int&
    int x = 42;
    IntRefHolder holder1{x};
    holder1.ref = 100;
    if (x != 100) return 1;
    
    // Test char&
    char c = 'A';
    CharRefHolder holder2{c};
    holder2.ref = 'Z';
    if (c != 'Z') return 2;
    
    // Test short&
    short s = 10;
    ShortRefHolder holder3{s};
    holder3.ref = 20;
    if (s != 20) return 3;
    
    // Test double&
    double d = 3.14;
    DoubleRefHolder holder4{d};
    holder4.ref = 6.28;
    if (d < 6.27 || d > 6.29) return 4;
    
    // Test struct&
    Point p;
    p.x = 1;
    p.y = 2;
    StructRefHolder holder5{p};
    holder5.ref.x = 10;
    holder5.ref.y = 20;
    if (p.x != 10 || p.y != 20) return 5;
    
    // Test template with int
    int ti = 5;
    RefWrapper<int> tw1{ti};
    tw1.ref = 15;
    if (ti != 15) return 6;
    
    // Test template with double
    double td = 1.5;
    RefWrapper<double> tw2{td};
    tw2.ref = 2.5;
    if (td < 2.4 || td > 2.6) return 7;
    
    // Test template with struct
    Point tp;
    tp.x = 3;
    tp.y = 4;
    RefWrapper<Point> tw3{tp};
    tw3.ref.x = 30;
    tw3.ref.y = 40;
    if (tp.x != 30 || tp.y != 40) return 8;
    
    return 0;  // All tests passed
}
