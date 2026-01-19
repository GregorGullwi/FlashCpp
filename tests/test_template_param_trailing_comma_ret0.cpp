// Test template parameter with trailing comma after default
template<typename T,
         typename U = T,
         typename V = int>
struct Test {
    T t;
    U u;
    V v;
};

int main() {
    Test<int> t;
    return 0;
}
