// Regression test: qualified member calls on instantiated templates in substitution context
// Previously, ExpressionSubstitutor had a dead else-if that could cause it to fall back
// to the raw base template name instead of the hash-based instantiated name, leading to
// lookup mismatches. The fix ensures get_instantiated_class_name() is called correctly.

template <typename T>
struct Box {
    T value;
    Box(T v) : value(v) {}
    T get() const { return value; }
    T doubled() const { return value * 2; }
};

template <typename T>
struct Pair {
    Box<T> first;
    Box<T> second;
    Pair(T a, T b) : first(a), second(b) {}
    T sum() const { return first.get() + second.get(); }
};

template <typename T>
T compute(T a, T b) {
    Pair<T> p(a, b);
    return p.first.get() + p.second.doubled();
}

int main() {
    // compute<int>(5, 15) = 5 + 30 = 35
    // But we want 42: compute<int>(12, 15) = 12 + 30 = 42
    int result = compute<int>(12, 15);
    return result;
}
