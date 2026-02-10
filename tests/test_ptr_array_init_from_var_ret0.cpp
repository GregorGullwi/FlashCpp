// Test pointer array initialization from named pointer variables.
// Validates that array_store correctly loads the value from a named variable
// (StringHandle) into the array element, not leaving the register uninitialized.

int main() {
int a = 42, b = 99;
int* pa = &a;
int* pb = &b;
int* arr[2] = {pa, pb};

// Verify that arr[0] == pa (points to a) and arr[1] == pb (points to b)
if (*arr[0] != 42) return 1;
if (*arr[1] != 99) return 2;

return 0;
}
