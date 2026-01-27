template<typename T>
struct Container {
    static const int size = 10;
    T value;
};

int main() {
    Container<int> c;
    c.value = 5;
    int result = Container<int>::size + c.value;  // Should be 15
    return result - 15;  // Should return 0
}