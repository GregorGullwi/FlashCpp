// Test gt ternary

template<int A, int B>
struct Math {
    bool greater() { return A > B; }
};

int main() {
    Math<10, 3> m;
    bool gt = m.greater();
    
    // 10 > 3 is true, so gt should be true (1), returning 1
    return gt ? 1 : 0;
}
