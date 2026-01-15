// Test: Object-like macros with parenthesized bodies containing sizeof
// This was incorrectly parsed as function-like macros before the fix.

// Object-like macro with parenthesized body containing sizeof
// On this system, sizeof(int) = 4, so SIZE_EXPR = 1024 / (8 * 4) = 1024 / 32 = 32
#define SIZE_EXPR (1024 / (8 * sizeof(int)))

// Array using the macro
int arr[SIZE_EXPR];

int main() {
    // SIZE_EXPR = 1024 / (8 * 4) = 32
    return SIZE_EXPR;
}
