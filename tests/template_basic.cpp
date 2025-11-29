// Basic template function test
// This tests parsing of simple function templates

template<typename T>
T max(T a, T b) {
    return (a > b) ? a : b;
}

template<typename T>
T min(T a, T b) {
    if (a < b) {
        return a;
    }
    return b;
}

// Template with multiple parameters
template<typename T, typename U>
T add(T a, U b) {
    return a + b;
}

// Non-type template parameter
template<int N>
int multiply_by_n(int x) {
    return x * N;
}

// Template with class keyword instead of typename
template<class T>
T identity(T value) {
    return value;
}

int main() {
    // For now, we're just testing parsing
    // Template instantiation will be implemented in Phase 2
    return 0;
}

