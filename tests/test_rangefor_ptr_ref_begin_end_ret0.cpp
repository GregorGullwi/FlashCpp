// Test range-for with int*& loop variable over a container with begin()/end()
// Validates that the reinterpret_cast target type copies pointer depth from loop variable
struct SinglePtrContainer {
    int* elem;
    int* sentinel;

    int** begin() { return &elem; }
    int** end() { return &sentinel; }
};

int main() {
    int a = 10;

    SinglePtrContainer c;
    c.elem = &a;
    c.sentinel = &a;

    // for (int*& p : c) iterates over the single int* element
    for (int*& p : c) {
        *p = *p + 1;
    }

    if (a != 11) return 1;
    return 0;
}
