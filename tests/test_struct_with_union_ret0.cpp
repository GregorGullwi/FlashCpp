// Test case: Struct containing union (simple members only)
// Status: ✅ PASSES - Anonymous unions can contain simple types
// Note: Arrays in anonymous unions don't work yet

union Data {
    int i;
    float f;
};

struct Container {
    int type;
    union {
        Data d;
        long long buffer;
    };
};

int main() {
    Container c;
    c.type = 1;
    c.d.i = 42;
    return 0;
}
