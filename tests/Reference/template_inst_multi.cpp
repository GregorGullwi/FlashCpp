// Template instantiation test - multiple types
// Tests that templates can be instantiated with different types
template<typename T>
T identity(T x) {
    return x;
}

int main() {
    int a = identity(42);
    float b = identity(3.14f);
    return a;
}

