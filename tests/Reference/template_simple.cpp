// Simplest possible template test
template<typename T>
T identity(T x) {
    return x;
}

int main() {
    int a = identity(42);
    int b = identity(3);
    return a + b - 45;
}
