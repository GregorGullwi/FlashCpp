// Test const in declaration but not in definition (and vice versa)

// Declaration with const parameter
int func1(const int x);

// Definition without const (should work - const only affects local copy)
int func1(int x) {
    return x + 1;
}

// Declaration without const
int func2(int x);

// Definition with const (should work)
int func2(const int x) {
    return x + 2;
}

// Declaration with const pointer
int func3(const int* ptr);

// Definition without const on the pointer itself (value is still const)
int func3(const int* ptr) {
    return *ptr;
}

// Declaration with pointer to const
void func4(const char* str);

// Definition matches
void func4(const char* str) {
    // str[0] = 'x';  // Would be error - str points to const
}

// Declaration with unnamed const parameter
int func5(const int);

// Definition with named parameter (const or not)
int func5(int value) {
    return value * 2;
}

int main() {
    int a = 10;
    int result = func1(a);
    result = func2(a);
    result = func3(&a);
    func4("test");
    result = func5(5);
    return result;
}
