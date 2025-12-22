// Test simple struct member initialization

struct Simple {
    int a = 10;
    int b = 20;
};

int main() {
    Simple s;
    return s.a + s.b;  // Should be 30
}
