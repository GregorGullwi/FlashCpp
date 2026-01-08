// Test case: Nested unions (anonymous inside anonymous)
// Status: ❌ FAILS - Parser doesn't support nested anonymous unions
// Error: "Expected type name after 'struct', 'class', or 'union'"

struct Outer {
    union {
        int i;
        union {
            float f;
            double d;
        };
    };
};

int main() {
    Outer o;
    o.i = 42;
    o.f = 3.14f;
    return 0;
}
