// Test recursive macro expansion prevention
#define RECURSIVE_A RECURSIVE_B
#define RECURSIVE_B RECURSIVE_A

// This should not expand infinitely - RECURSIVE_A should expand to RECURSIVE_B and stop
int main() {
    RECURSIVE_A;
    return 0;
}
