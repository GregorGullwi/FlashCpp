// Verify: lazy materialized conversion operator works for multiple concrete types.
// Expected return: 0

template<typename T>
struct LazyWrapper {
    using value_type = T;
    T value_;
    LazyWrapper(T v) : value_(v) {}
    operator value_type() const { return value_; }
};

int main() {
    LazyWrapper<int>   wi(10);
    LazyWrapper<short> ws(5);
    int  a = wi;   // 10
    int  b = ws;   // 5
    return a - b - 5;  // 10 - 5 - 5 = 0
}
