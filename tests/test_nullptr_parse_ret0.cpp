// Test nullptr parsing in member initializer
// Don't create any instances to avoid constructor issues

struct TestNullptr {
    int* ptr = nullptr;
    int value = 42;
};

int main() {
    return 0;
}
