// Test for pointer to 2D array element
// Expected return: 42

int main() {
    int arr[3][3];
    arr[0][0] = 10;
    arr[1][1] = 20;
    arr[2][2] = 12;
    
    // Get pointers to diagonal elements
    int* p0 = &arr[0][0];
    int* p1 = &arr[1][1];
    int* p2 = &arr[2][2];
    
    // Sum through pointers
    return *p0 + *p1 + *p2;  // 10 + 20 + 12 = 42
}
