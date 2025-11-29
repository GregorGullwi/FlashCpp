// Test const pointer alias

template<typename T>
using ConstPtr = const T*;

int main() {
    int x = 42;
    ConstPtr<int> p = &x;
    return *p;
}
