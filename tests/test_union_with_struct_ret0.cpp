// Test case: Union containing a struct
// Status: ✅ PASSES - Unions can contain struct members

struct Point {
    int x;
    int y;
};

struct Data {
    union {
        int i;
        float f;
        Point p;
    };
};

int main() {
    Data d;
    d.i = 42;
    d.p.x = 10;
    d.p.y = 20;
    return 0;
}
