// Test: Compound assignment operators on bitfield members
struct Flags {
    unsigned a : 4 = 3;
    unsigned b : 4 = 2;
};

int main() {
    Flags f;
    f.a += 2;  // 3 + 2 = 5
    f.b -= 1;  // 2 - 1 = 1
    return f.a * 10 + f.b;  // 51
}
