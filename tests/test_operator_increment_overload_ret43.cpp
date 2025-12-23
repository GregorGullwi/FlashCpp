// Test operator++ (prefix and postfix) overload resolution
// This demonstrates iterator-like increment behavior

struct Counter {
    int value;
    
    // Prefix increment: ++obj
    Counter& operator++() {
        value++;
        return *this;
    }
    
    // Postfix increment: obj++
    Counter operator++(int) {
        Counter temp;
        temp.value = value;
        value++;
        return temp;
    }
};

int main() {
    Counter c1;
    c1.value = 10;
    
    // Prefix increment
    ++c1;  // value becomes 11
    
    // Postfix increment
    c1++;  // value becomes 12
    
    // Use prefix increment in expression
    Counter& ref = ++c1;  // value becomes 13, ref points to c1
    
    // Calculate result: 13 + 30 = 43
    return c1.value + 30;
}
