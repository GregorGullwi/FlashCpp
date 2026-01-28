// Test nullptr in regular struct member

struct Container {
    int* ptr;
    
    int testNull() {
        ptr = nullptr;
        if (ptr) {
            return 1;  // non-null
        }
        return 0;  // null
    }
};

int main() {
    Container c;
    return c.testNull();
}
