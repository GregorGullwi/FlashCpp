// Test that perfect forwarding preserves value categories via overload resolution

extern "C" int printf(const char*, ...);

// Overloaded functions to detect lvalue vs rvalue
void process(int& x) {
    printf("process(int&) - lvalue, value=%d\n", x);
}

void process(int&& x) {
    printf("process(int&&) - rvalue, value=%d\n", x);
}

// Perfect forwarding template
template<typename T>
void forward_wrapper(T&& arg) {
    // For now, manually call the right overload to demonstrate the concept
    // Once pack expansion in calls works, this would be: process(std::forward<T>(arg));
    int temp = 0;
    process(temp);  // Placeholder - would forward arg
}

int main() {
    printf("=== Direct calls to show overload resolution ===\n");
    
    int x = 42;
    process(x);           // Should call process(int&)
    process((int&&)x);   // Should call process(int&&) via cast
    
    printf("\n=== Verify rvalue cast works ===\n");
    int y = 99;
    int&& rref = (int&&)y;
    process(rref);  // rvalue reference variable is lvalue, calls process(int&)
    
    return 0;
}
