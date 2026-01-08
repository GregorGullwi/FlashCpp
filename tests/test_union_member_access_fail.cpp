// Test case: Accessing union members causes infinite loop/hang
// Status: ❌ FAILS - Compilation hangs indefinitely
// Bug: Member access to union fields in structs causes parser/codegen to hang
// This is a critical bug that needs investigation

struct MyStruct {
    union {
        int i;
        float f;
    } data;
};

int main() {
    MyStruct s;
    s.data.i = 42;  // This line causes the hang
    return s.data.i == 42 ? 0 : 1;
}
