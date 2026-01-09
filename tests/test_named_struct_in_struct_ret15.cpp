// Test case: Named struct in struct (similar to union)
// Expected return: 15

struct OuterStruct {
    struct InnerStruct {
        int x;
        int y;
    } inner;  // Named struct member
    
    int z;
};

int main() {
    OuterStruct s;
    s.inner.x = 5;
    s.inner.y = 10;
    s.z = 20;
    return s.inner.x + s.inner.y;  // Should return 15
}
