// Test dereferencing a pointer member without templates

struct Container {
    int* ptr;
    const int* const_ptr;
};

int main() {
    Container c;
    int x = 42;
    c.ptr = &x;
    c.const_ptr = &x;
    if (*c.ptr != 42) return 1;
    if (*c.const_ptr != 42) return 2;
    return 0;
}
