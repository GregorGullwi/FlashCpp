// Template instantiation test - simple case
// Tests basic template instantiation with a single type parameter
template<typename T>
T max(T a, T b) {
    if (a > b) return a;
    return b;
}

int main() {
    int x = max(3, 5);
    return x;
}

