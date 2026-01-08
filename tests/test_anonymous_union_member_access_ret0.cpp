// Test case: Accessing anonymous union members
// Status: ✅ PASSES - Fixed by implementing proper anonymous union member flattening
// Anonymous union members are now properly added to the parent struct member lookup

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
    s.i = 42;  // This now works correctly
    return s.i == 42 ? 0 : 1;
}
