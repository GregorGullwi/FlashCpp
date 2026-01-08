// Test case: Accessing anonymous union members causes infinite loop/hang
// Status: ❌ FAILS - Compilation hangs indefinitely
// Bug: Member access to anonymous union fields causes parser/codegen to hang
// This is a critical bug that prevents <optional> and <variant> from working

struct MyStruct {
    int type;
    union {
        int i;
        float f;
    };
};

int main() {
    MyStruct s;
    s.type = 1;
    s.i = 42;  // This line causes the hang
    return s.i == 42 ? 0 : 1;
}
