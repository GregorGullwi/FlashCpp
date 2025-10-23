// Test: extern "C" single declaration
// This tests extern "C" linkage for a single function

extern "C" int add(int a, int b);

extern "C" int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(10, 20);
    return result;
}

