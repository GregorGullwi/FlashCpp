// Test named anonymous union (inline anonymous union with member name)
// Pattern: union { ... } member_name;

struct Container {
    union {
        int i;
        float f;
    } data;
    
    int get_as_int() {
        return data.i;
    }
};

int main() {
    Container c;
    c.data.i = 42;
    return c.data.i;  // Should return 42
}
