// Test: new T[n] with n exceeding the constexpr evaluation limit must be rejected
// to prevent compiler OOM crashes.

constexpr int too_big() {
    int* p = new int[1000001];
    delete[] p;
    return 0;
}
static_assert(too_big() == 0);

int main() { return 0; }
