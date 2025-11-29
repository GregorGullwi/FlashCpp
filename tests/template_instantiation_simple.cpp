// Simple template instantiation test
template<typename T>
T max(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    int x = max(3, 5);  // Should instantiate max<int>
    return x;
}

