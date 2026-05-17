template <typename T>
constexpr T wrap_add(T a, T b) {
    return a + b;
}

int main() {
    constexpr unsigned char v = wrap_add<unsigned char>(250, 10);
    static_assert(v == static_cast<unsigned char>(4));
    return static_cast<int>(v) - 4;
}
