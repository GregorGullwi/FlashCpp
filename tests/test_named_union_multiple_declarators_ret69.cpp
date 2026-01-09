// Test case: Named union with multiple variable declarators
// Expected return: 69

struct Container {
    union Data {
        int i;
        float f;
    } data1, data2;  // Multiple named union members
    
    int value;
};

int main() {
    Container c;
    c.data1.i = 42;
    c.data2.i = 27;
    c.value = 100;
    return c.data1.i + c.data2.i;  // Should return 69
}
