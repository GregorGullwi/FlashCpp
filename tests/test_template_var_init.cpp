// Test template function call in variable initialization

template<typename T>
T identity(T x) {
    return x;
}

int main() {
    int a = identity(5);
    return a;
}
