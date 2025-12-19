// Comprehensive RVO test with various scenarios

struct Data {
    int a;
    int b;
    
    Data(int x, int y) : a(x), b(y) {}
};

// Test 1: Simple RVO with temporary
Data test1() {
    return Data(10, 20);
}

// Test 2: RVO with expression in constructor
Data test2(int x) {
    return Data(x + 5, x * 2);
}

// Test 3: Conditional return with temporaries (both prvalues)
Data test3(int flag) {
    if (flag > 0) {
        return Data(1, 2);
    } else {
        return Data(3, 4);
    }
}

int main() {
    // Test 1
    Data d1 = test1();
    if (d1.a != 10 || d1.b != 20) return 1;
    
    // Test 2
    Data d2 = test2(5);
    if (d2.a != 10 || d2.b != 10) return 2;
    
    // Test 3
    Data d3 = test3(1);
    if (d3.a != 1 || d3.b != 2) return 3;
    
    Data d4 = test3(-1);
    if (d4.a != 3 || d4.b != 4) return 4;
    
    return 0;
}
