// Simple operator() test

struct Adder {
    int value;
    
    int operator()(int x) {
        return value + x;
    }
};

int main() {
    Adder a;
    a.value = 10;
    int result = a(5);
    return result;  // Should return 15
}

