// Test operator+ overload resolution
// This demonstrates simple arithmetic operator overload

struct Number {
    int value;
    
    // Binary addition operator
    Number operator+(const Number& other) {
        Number result;
        result.value = value + other.value;
        return result;
    }
};

int main() {
    Number a;
    a.value = 5;
    Number b;
    b.value = 10;
    
    // Use operator+
    Number c = a + b;  // Should be 5 + 10 = 15
    
    return c.value;
}
