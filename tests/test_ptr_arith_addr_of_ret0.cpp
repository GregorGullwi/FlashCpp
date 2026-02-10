// Test pointer arithmetic on address-of expressions.
// Validates that &arr[0] + N scales by sizeof(element), not by 1 byte.
// Also tests &member + N for struct members that are pointers.

struct PtrPair {
int* first;
int* second;
int** end() { return &second + 1; }
};

int main() {
// Basic: &arr[0] + 2 should advance by 2*sizeof(int) = 8 bytes
int arr[3] = {10, 20, 30};
int* p = &arr[0] + 2;
if (*p != 30) return 1;

// Pointer-to-pointer: &member + 1 should advance by sizeof(int*) = 8 bytes
int a = 10, b = 20;
PtrPair pair;
pair.first = &a;
pair.second = &b;

int** begin = &pair.first;
int** end = pair.end();
long count = end - begin;
if (count != 2) return 2;

return 0;
}
