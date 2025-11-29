// Test reference alias

template<typename T>
using Ref = T&;

int main() {
    int x = 42;
    Ref<int> r = x;
    return r;
}
