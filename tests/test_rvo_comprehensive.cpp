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

// Test 4: RVO with function call as argument
int helper() {
    return 10;
}

Data test4() {
    return Data(helper(), 40);
}

// Test 5: RVO with multiple expressions and conditionals
Data test5(int x) {
    return Data(x + x, (x > 0) ? x * 2 : x + 1);
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
    
    // Test 4: RVO with function call
    Data d5 = test4();
    if (d5.a != 10 || d5.b != 40) return 5;
    
    // Test 5: RVO with conditional expression
    Data d6 = test5(7);
    if (d6.a != 14 || d6.b != 14) return 6;
    
    Data d7 = test5(-2);
    if (d7.a != -4 || d7.b != -1) return 7;
    
    return 0;
}
