// Test anonymous typename parameter with default
template<typename T,
         typename = int>
struct Test {
    T t;
};

int main() {
    Test<int> t;
    return 0;
}
