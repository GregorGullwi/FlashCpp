// Test pointer member initialization with nullptr

struct WithPointers {
    int* ptr1 = nullptr;
    int value = 42;
};

int main() {
    WithPointers w;
    
    int check = (w.ptr1 == nullptr) ? 1 : 0;
    
    return w.value + check;  // Should be 43
}
