// Test function pointer member call using global struct

int getVal() {
    return 15;
}

struct WithFuncPtr {
    int (*func)();
    int value;
};

// Global instance - no constructor call needed
WithFuncPtr global_w;

int main() {
    global_w.func = getVal;
    global_w.value = 10;
    
    int result = global_w.func();
    
    return result + global_w.value;  // Should be 25
}
