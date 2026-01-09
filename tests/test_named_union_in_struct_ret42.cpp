// Test case: Named union in struct - access via union name
// Expected return: 42

struct MyStruct {
    union Data {
        int i;
        float f;
    } data;  // Named union member
    
    int other;
};

int main() {
    MyStruct s;
    s.data.i = 42;  // Access through union name
    s.other = 10;
    return s.data.i;
}
