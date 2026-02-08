// Test: Unnamed array reference parameters (const T (&)[N])
// Verifies the parser handles the unnamed reference-to-array pattern
void accept_array(const int (&)[5]) {}

int main() {
    int arr[5] = {1, 2, 3, 4, 42};
    accept_array(arr);
    return arr[4];
}
