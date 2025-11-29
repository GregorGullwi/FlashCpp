// Test function pointer member call WITHOUT initialization

int getVal() {
    return 15;
}

struct WithFuncPtr {
    int (*func)();
    int value;
};

int main() {
    WithFuncPtr w;
    w.func = getVal;
    w.value = 10;
    
    int result = w.func();
    
    return result + w.value;  // Should be 25
}
