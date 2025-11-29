// Test const pointer in regular struct (should crash)

struct Container {
    const int* const_ptr;
};

int main() {
    Container c;
    int x = 42;
    c.const_ptr = &x;
    if (*c.const_ptr != 42) return 1;
    return 0;
}
