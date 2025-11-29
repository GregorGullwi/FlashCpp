// Test function pointer in regular struct
struct Test {
    int (*operation)(int, int);
};

int main() {
    Test t;
    return 0;
}
