// Test case: Named union declaration works
// Status: ✅ PASSES - Named unions can be declared

struct MyStruct {
    union Data {
        int i;
        float f;
    } data;
};

int main() {
    MyStruct s;
    return 0;
}
