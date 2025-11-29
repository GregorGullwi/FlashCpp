// Test simple alias template

template<typename T>
using Ptr = T*;

int main() {
    int x = 42;
    Ptr<int> p = &x;
    return *p;
}
