// Test case for deeply nested anonymous struct/union in typedef declarations
// This pattern is used in system headers like <csignal> for siginfo_t

typedef struct {
    int outer_val;
    union {
        int pad[4];
        struct {
            int inner_val;
            union {
                struct {
                    void* lower;
                    void* upper;
                } bounds;
                int key;
            } nested_union;
        } inner_struct;
    } outer_union;
} DeepNested;

int main() {
    DeepNested d;
    d.outer_val = 10;
    d.outer_union.inner_struct.inner_val = 20;
    d.outer_union.inner_struct.nested_union.bounds.lower = 0;
    d.outer_union.inner_struct.nested_union.key = 42;
    return d.outer_union.inner_struct.nested_union.key - 42;  // Returns 0
}
