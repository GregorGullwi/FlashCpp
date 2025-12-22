// Test pointer member initialization with nullptr (no ternary)

struct WithPointers {
    int* ptr1 = nullptr;
    int value = 42;
};

int main() {
    WithPointers w;
    
    int check = (w.ptr1 == nullptr);
    
    return w.value + check;  // Should be 43
}
