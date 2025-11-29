// Comprehensive test for temp variable handling in member functions
struct Calculator {
    int base;
    
    int add(int a, int b) {
        int sum = a + b;
        return sum + base;
    }
    
    int multiply(int a, int b) {
        int product = a * b;
        int temp = product + base;
        return temp;
    }
    
    int complex(int x) {
        int temp1 = x + base;
        int temp2 = temp1 * 2;
        int temp3 = temp2 - 5;
        return temp3;
    }
};

int main() {
    Calculator calc;
    calc.base = 10;
    
    int result1 = calc.add(5, 3);      // (5 + 3) + 10 = 18
    int result2 = calc.multiply(4, 2); // (4 * 2) + 10 = 18
    int result3 = calc.complex(7);     // ((7 + 10) * 2) - 5 = 29
    
    return result1 + result2 + result3; // 18 + 18 + 29 = 65
}
