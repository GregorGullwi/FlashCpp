// Test different array types in anonymous unions
struct MultiArray {
    int type;
    union {
        char chars[10];
        int ints[5];
        float floats[3];
        double doubles[2];
    };
};

int main() {
    MultiArray m;
    
    // Test int array
    m.type = 1;
    m.ints[0] = 10;
    m.ints[1] = 20;
    m.ints[2] = 12;
    
    int sum = m.ints[0] + m.ints[1] + m.ints[2];  // 10 + 20 + 12 = 42
    
    return sum;
}
