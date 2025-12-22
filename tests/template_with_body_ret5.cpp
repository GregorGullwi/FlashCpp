// Template with function body
// Tests that template parameters can be used in function bodies
template<typename T>
T max(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    int x = max(3, 5);
    return x;
}

