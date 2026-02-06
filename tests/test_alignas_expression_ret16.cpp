// Test: alignas with expression arguments
// Validates parsing of alignas(N) with constant expression alignment values
struct alignas(16) AlignedStruct {
    int x;
};

int main() {
    return alignof(AlignedStruct);  // 16
}
