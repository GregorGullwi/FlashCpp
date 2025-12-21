// Test for constructor/default initialization bug with addressof operator
// BUG: Returns wrong value when taking address of member in default-initialized struct array
// Expected: 10
// Actual: 64 (or other garbage value)
// Related to: AddressOf member access fixes

struct S {
    int x{10};
    int* p = nullptr;
};

int main() {
    // Test with 16-byte struct (size that requires IMUL)
    S arr[3]{};
    int i = 1;
    arr[0].p = &arr[i].x;
    return *arr[0].p;  // Should return 10
}
