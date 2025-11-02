// Simplest possible template test
template<typename T>
T identity(T x) {
    return x;
}

int main() {
    int a = identity(42);
    float b = identity(3.14f);
    return a + static_cast<int>(b);
}

