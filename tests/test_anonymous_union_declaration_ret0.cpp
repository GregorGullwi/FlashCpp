// Test case: Regular anonymous union declaration works
// Status: ✅ PASSES - Anonymous unions can be declared

struct MyStruct {
    union {
        int i;
        float f;
    };
};

int main() {
    MyStruct s;
    return 0;
}
