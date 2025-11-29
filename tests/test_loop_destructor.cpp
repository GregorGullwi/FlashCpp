// Test: Loop with object creation and destruction
// Should create and destroy object in each iteration

extern "C" int printf(const char*, ...);

struct Object {
    int id;
    Object(int i) : id(i) {
        printf("  Constructor: obj%d\n", id);
    }
    ~Object() {
        printf("  Destructor: obj%d\n", id);
    }
};

int main() {
    printf("Before loop\n");
    for (int i = 0; i < 3; i++) {
        printf("Iteration %d\n", i);
        Object obj(i);
    }
    printf("After loop\n");
    return 0;
}
