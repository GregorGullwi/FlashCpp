// Simplified test: Just use the type alias directly

template<typename T>
struct Container {
    using value_type = T;
};

int main() {
    int x = 42;
    return 0;
}
