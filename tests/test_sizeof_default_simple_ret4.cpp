// Test: sizeof in non-type template parameter default
template<typename T, int N = sizeof(T)>
struct test {
    int value;
};

int main() {
    test<int> t;  // N should be sizeof(int) = 4
    t.value = 4;
    return t.value;  // Should return 4
}
