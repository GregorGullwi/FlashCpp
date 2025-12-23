// Test returning a pointer from a regular function

int* get_pointer() {
    static int value = 100;
    return &value;
}

int main() {
    int* p = get_pointer();
    return *p;  // Should return 100
}
