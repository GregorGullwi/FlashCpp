// Test function pointer member initialization

int getVal() {
    return 15;
}

struct WithFuncPtr {
    int (*func)() = getVal;
    int value = 10;
};

int main() {
    WithFuncPtr w;
    
    int result = w.func();
    
    return result + w.value;  // Should be 25
}
