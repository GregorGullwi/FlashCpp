// Test struct with multiple anonymous unions
struct MultiUnion {
    union {
        int i;
        float f;
    };
    union {
        char c;
        int j;
    };
};

int main() {
    MultiUnion m;
    m.i = 10;
    m.j = 20;
    return m.i + m.j;  // 10 + 20 = 30
}
