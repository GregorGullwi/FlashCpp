// Test case: Named union member access (union with a member name)
// Status: ❌ NOT SUPPORTED - Named unions are not implemented
// Error: "Expected type name after 'struct', 'class', or 'union'"
//
// Named unions like `union {...} data;` require creating a nested union type
// and then a member of that type. This is more complex than anonymous unions
// and is not currently supported by the parser.
//
// Workaround: Use anonymous unions or separate the union declaration from the member

struct MyStruct {
    union {
        int i;
        float f;
    } data;  // 'data' makes this a named union member
};

int main() {
    MyStruct s;
    s.data.i = 42;  // This doesn't work - named unions not supported
    return s.data.i == 42 ? 0 : 1;
}
