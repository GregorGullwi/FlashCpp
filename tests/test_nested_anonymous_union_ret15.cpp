// Test nested anonymous unions
struct Outer {
    int x;
    union {
        int a;
        union {
            int b;
            float c;
        };
    };
};

int main() {
    Outer o;
    o.x = 10;
    o.b = 5;  // Access nested anonymous union member
    return o.x + o.b;  // 10 + 5 = 15
}
