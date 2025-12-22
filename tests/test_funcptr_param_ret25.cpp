// Test function pointer member call using parameter

int getVal() {
    return 15;
}

struct WithFuncPtr {
    int (*func)();
    int value;
};

int test(WithFuncPtr* w) {
    w->func = getVal;
    w->value = 10;
    
    int result = w->func();
    
    return result + w->value;  // Should be 25
}

int main() {
    WithFuncPtr w;
    return test(&w);
}
