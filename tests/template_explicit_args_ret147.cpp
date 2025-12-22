// Template with explicit template arguments
// Tests explicit template argument syntax: func<Type>(args)
template<typename T>
T identity(T x) {
    return x;
}

template<typename T>
T max(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    // Explicit template arguments
    int x = identity<int>(42);
    int y = max<int>(3, 5);

    // Should also work with deduced arguments
    int z = identity(100);

    return x + y + z;
}

