// Test explicit constructors with various return value scenarios

class Wrapper {
public:
    explicit Wrapper(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

// Test 1: Return by explicitly constructing
Wrapper returnExplicit(int x) {
    return Wrapper(x);  // OK: direct initialization
}

// Test 2: Return value with braced initialization
Wrapper returnBraced(int x) {
    return Wrapper{x};  // OK: direct list initialization
}

// Test 3: Return temporary
Wrapper returnTemp() {
    return Wrapper(99);  // OK: direct initialization
}

// Test 4: Multiple return paths
Wrapper conditionalReturn(bool flag) {
    if (flag) {
        return Wrapper(10);  // OK
    } else {
        return Wrapper(20);  // OK
    }
}

// Test 5: Return value passed to another function
int chainedCall() {
    Wrapper w = returnExplicit(30);  // OK: return value can be used in copy init
    return w.value();
}

int main() {
    Wrapper w1 = returnExplicit(5);     // OK
    Wrapper w2 = returnBraced(10);      // OK
    Wrapper w3 = returnTemp();          // OK
    Wrapper w4 = conditionalReturn(true);  // OK
    int result = chainedCall();         // OK
    
    // Calculate: 5 + 10 + 99 + 10 + 30 = 154
    int sum = w1.value() + w2.value() + w3.value() + w4.value() + result;
    
    return sum == 154 ? 0 : 1;
}
