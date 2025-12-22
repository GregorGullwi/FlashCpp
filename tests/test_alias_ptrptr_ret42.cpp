// Test double pointer alias

template<typename T>
using PtrPtr = T**;

int main() {
    int x = 42;
    int* p = &x;
    PtrPtr<int> pp = &p;
    return **pp;
}
