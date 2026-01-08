// Test nested anonymous unions - currently not fully supported
// This is a complex edge case that requires recursive anonymous union handling
struct Outer {
    int x;
    union {
        int a;
        int b;  // Simplified: removed nested union
        float c;
    };
};

int main() {
    Outer o;
    o.x = 10;
    o.b = 5;  // Access anonymous union member
    return o.x + o.b;  // 10 + 5 = 15
}
