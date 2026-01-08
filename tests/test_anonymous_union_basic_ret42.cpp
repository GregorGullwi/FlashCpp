// Test basic anonymous union within a struct
struct Data {
    int type;
    union {
        int i;
        float f;
        char c;
    };
};

int main() {
    Data d;
    d.type = 1;
    d.i = 42;  // Access anonymous union member directly
    return d.i;
}
